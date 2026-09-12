#include "LoopbackCapture.h"

#include <core/text/Utf8.h>

#if JUCE_WINDOWS
 #include <windows.h>
 #include <mmdeviceapi.h>
 #include <audioclient.h>
 #include <functiondiscoverykeys_devpkey.h>
#endif

namespace keyla::app
{

using keyla::operator""_u8;

namespace
{
    const char* defaultLabel = "(la salida predeterminada)";
}

#if JUCE_WINDOWS

namespace
{
    // Los dos subformatos que entrega WASAPI en la práctica. Se declaran aquí
    // en vez de arrastrar <ksmedia.h>, que mete medio subsistema de streaming
    // dentro de una unidad de traducción de JUCE.
    const GUID kSubFormatFloat { 0x00000003, 0x0000, 0x0010,
                                 { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
    const GUID kSubFormatPcm   { 0x00000001, 0x0000, 0x0010,
                                 { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

    /** COM mientras dure el ámbito, y sólo si lo abrimos nosotros. */
    struct ComScope
    {
        ComScope() : result (CoInitializeEx (nullptr, COINIT_MULTITHREADED)) {}
        ~ComScope() { if (SUCCEEDED (result)) CoUninitialize(); }

        bool usable() const { return SUCCEEDED (result) || result == RPC_E_CHANGED_MODE; }

        HRESULT result;
    };

    template <typename T>
    void release (T*& pointer)
    {
        if (pointer != nullptr)
        {
            pointer->Release();
            pointer = nullptr;
        }
    }

    juce::String friendlyNameOf (IMMDevice* device)
    {
        IPropertyStore* properties = nullptr;

        if (device == nullptr || FAILED (device->OpenPropertyStore (STGM_READ, &properties)))
            return {};

        PROPVARIANT value;
        PropVariantInit (&value);

        juce::String name;

        if (SUCCEEDED (properties->GetValue (PKEY_Device_FriendlyName, &value))
            && value.vt == VT_LPWSTR && value.pwszVal != nullptr)
            name = juce::String (value.pwszVal);

        PropVariantClear (&value);
        properties->Release();

        return name;
    }

    /** Cuántos bits y de qué tipo, resuelto ya sea el formato simple o el
        extensible. Devuelve 0 si no se sabe convertir. */
    struct SampleLayout
    {
        bool isFloat { false };
        int bits { 0 };
    };

    SampleLayout layoutOf (const WAVEFORMATEX& format)
    {
        SampleLayout layout;

        if (format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
        {
            layout.isFloat = true;
            layout.bits = format.wBitsPerSample;
        }
        else if (format.wFormatTag == WAVE_FORMAT_PCM)
        {
            layout.bits = format.wBitsPerSample;
        }
        else if (format.wFormatTag == WAVE_FORMAT_EXTENSIBLE
                 && format.cbSize >= sizeof (WAVEFORMATEXTENSIBLE) - sizeof (WAVEFORMATEX))
        {
            const auto& extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE&> (format);

            if (IsEqualGUID (extensible.SubFormat, kSubFormatFloat))
            {
                layout.isFloat = true;
                layout.bits = format.wBitsPerSample;
            }
            else if (IsEqualGUID (extensible.SubFormat, kSubFormatPcm))
            {
                layout.bits = format.wBitsPerSample;
            }
        }

        return layout;
    }
}

/** Todo lo que hay que soltar al cerrar, en un sitio. */
struct LoopbackCapture::Session
{
    IMMDeviceEnumerator* enumerator { nullptr };
    IMMDevice* device { nullptr };
    IAudioClient* client { nullptr };
    IAudioCaptureClient* capture { nullptr };
    WAVEFORMATEX* format { nullptr };

    SampleLayout layout;
    int channels { 2 };

    void close()
    {
        if (client != nullptr)
            client->Stop();

        release (capture);
        release (client);
        release (device);
        release (enumerator);

        if (format != nullptr)
        {
            CoTaskMemFree (format);
            format = nullptr;
        }
    }
};

LoopbackCapture::LoopbackCapture() : juce::Thread ("keyla-loopback") {}

LoopbackCapture::~LoopbackCapture()
{
    stop();
}

juce::String LoopbackCapture::defaultOutputLabel()
{
    return juce::String (juce::CharPointer_UTF8 (defaultLabel));
}

juce::StringArray LoopbackCapture::availableOutputs()
{
    juce::StringArray names;
    names.add (defaultOutputLabel());

    ComScope com;

    if (! com.usable())
        return names;

    IMMDeviceEnumerator* enumerator = nullptr;

    if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof (IMMDeviceEnumerator),
                                  reinterpret_cast<void**> (&enumerator))))
        return names;

    IMMDeviceCollection* collection = nullptr;

    if (SUCCEEDED (enumerator->EnumAudioEndpoints (eRender, DEVICE_STATE_ACTIVE, &collection)))
    {
        UINT count = 0;
        collection->GetCount (&count);

        for (UINT i = 0; i < count; ++i)
        {
            IMMDevice* device = nullptr;

            if (SUCCEEDED (collection->Item (i, &device)))
            {
                const auto name = friendlyNameOf (device);

                if (name.isNotEmpty())
                    names.add (name);

                device->Release();
            }
        }

        collection->Release();
    }

    enumerator->Release();
    return names;
}

juce::String LoopbackCapture::openSession (Session& session, const juce::String& deviceName)
{
    if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof (IMMDeviceEnumerator),
                                  reinterpret_cast<void**> (&session.enumerator))))
        return "Windows no dejo enumerar los dispositivos de audio."_u8;

    const bool wantsDefault = deviceName.isEmpty() || deviceName == defaultOutputLabel();

    // **eMultimedia, no eConsole.** Windows mantiene tres predeterminados a la
    // vez: consola, multimedia y comunicaciones. En este equipo el de consola
    // es "Sonar - Chat" —el de las llamadas— y escucharlo no daba ni un
    // sample, porque la musica no sale por ahi. El que reproduce musica es el
    // de multimedia, y es el unico que tiene sentido escuchar aqui.
    if (wantsDefault)
    {
        if (FAILED (session.enumerator->GetDefaultAudioEndpoint (eRender, eMultimedia, &session.device)))
            return "No hay salida predeterminada de Windows."_u8;
    }
    else
    {
        // Por nombre, nunca por indice: la lista de dispositivos se reordena
        // sola en cuanto enchufas cualquier cosa (doc 01 §1.1).
        IMMDeviceCollection* collection = nullptr;

        if (FAILED (session.enumerator->EnumAudioEndpoints (eRender, DEVICE_STATE_ACTIVE, &collection)))
            return "Windows no dejo enumerar las salidas."_u8;

        UINT count = 0;
        collection->GetCount (&count);

        for (UINT i = 0; i < count && session.device == nullptr; ++i)
        {
            IMMDevice* device = nullptr;

            if (SUCCEEDED (collection->Item (i, &device)))
            {
                if (friendlyNameOf (device) == deviceName)
                    session.device = device;         // se queda con la referencia
                else
                    device->Release();
            }
        }

        collection->Release();

        if (session.device == nullptr)
            return "Ya no esta la salida \""_u8 + deviceName + "\".";
    }

    {
        const juce::ScopedLock lock (nameLock);
        openedDevice = wantsDefault ? friendlyNameOf (session.device) : deviceName;
    }

    if (FAILED (session.device->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr,
                                          reinterpret_cast<void**> (&session.client))))
        return "No se pudo abrir el cliente de audio de esa salida."_u8;

    if (FAILED (session.client->GetMixFormat (&session.format)) || session.format == nullptr)
        return "No se pudo leer el formato de esa salida."_u8;

    session.layout = layoutOf (*session.format);
    session.channels = juce::jmax (1, static_cast<int> (session.format->nChannels));

    if (session.layout.bits == 0)
        return "Esa salida usa un formato que Keyla no sabe leer."_u8;

    // 200 ms de buffer: de sobra para un sondeo relajado y poca memoria.
    constexpr REFERENCE_TIME bufferDuration = 2'000'000;

    const auto hr = session.client->Initialize (AUDCLNT_SHAREMODE_SHARED,
                                                AUDCLNT_STREAMFLAGS_LOOPBACK,
                                                bufferDuration, 0, session.format, nullptr);

    if (FAILED (hr))
    {
        if (hr == AUDCLNT_E_DEVICE_IN_USE)
            return "Esa salida esta ocupada en modo exclusivo, y lo que esta en "
                   "exclusivo no se puede escuchar. Suele ser la propia Keyla: "
                   "quita el modo exclusivo, o escucha otra salida."_u8;

        return "Windows no dejo abrir la escucha de esa salida."_u8;
    }

    if (FAILED (session.client->GetService (__uuidof (IAudioCaptureClient),
                                            reinterpret_cast<void**> (&session.capture))))
        return "No se pudo obtener el cliente de captura."_u8;

    if (FAILED (session.client->Start()))
        return "Windows no dejo arrancar la captura."_u8;

    capturedRate.store (session.format->nSamplesPerSec, std::memory_order_relaxed);
    return {};
}

void LoopbackCapture::run()
{
    ComScope com;

    Session session;
    juce::String error;

    if (! com.usable())
        error = "Windows no dejo inicializar COM."_u8;
    else
        error = openSession (session, wantedDevice);

    startupError = error;
    capturing.store (error.isEmpty(), std::memory_order_relaxed);
    startupDone.signal();

    if (! error.isEmpty())
    {
        session.close();
        return;
    }

    while (! threadShouldExit())
    {
        UINT32 packetFrames = 0;

        if (FAILED (session.capture->GetNextPacketSize (&packetFrames)))
            break;

        if (packetFrames == 0)
        {
            // Nada que leer. En loopback esto es lo **normal** cuando no hay
            // ningún programa reproduciendo: Windows no entrega silencio, no
            // entrega nada. Se espera un poco y se vuelve a mirar.
            wait (8);
            continue;
        }

        while (packetFrames != 0 && ! threadShouldExit())
        {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;

            if (FAILED (session.capture->GetBuffer (&data, &frames, &flags, nullptr, nullptr)))
                break;

            if (frames > 0)
            {
                if (monoScratch.size() < frames)
                    monoScratch.resize (frames);

                const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                const int channels = session.channels;

                if (silent || data == nullptr)
                {
                    std::fill (monoScratch.begin(), monoScratch.begin() + frames, 0.0f);
                }
                else if (session.layout.isFloat && session.layout.bits == 32)
                {
                    const auto* source = reinterpret_cast<const float*> (data);

                    for (UINT32 i = 0; i < frames; ++i)
                    {
                        float sum = 0.0f;

                        for (int c = 0; c < channels; ++c)
                            sum += source[i * channels + c];

                        monoScratch[i] = sum / channels;
                    }
                }
                else if (! session.layout.isFloat && session.layout.bits == 16)
                {
                    const auto* source = reinterpret_cast<const std::int16_t*> (data);

                    for (UINT32 i = 0; i < frames; ++i)
                    {
                        float sum = 0.0f;

                        for (int c = 0; c < channels; ++c)
                            sum += source[i * channels + c] / 32768.0f;

                        monoScratch[i] = sum / channels;
                    }
                }
                else if (! session.layout.isFloat && session.layout.bits == 32)
                {
                    const auto* source = reinterpret_cast<const std::int32_t*> (data);

                    for (UINT32 i = 0; i < frames; ++i)
                    {
                        double sum = 0.0;

                        for (int c = 0; c < channels; ++c)
                            sum += source[i * channels + c] / 2147483648.0;

                        monoScratch[i] = static_cast<float> (sum / channels);
                    }
                }
                else
                {
                    std::fill (monoScratch.begin(), monoScratch.begin() + frames, 0.0f);
                }

                if (onAudio != nullptr)
                    onAudio (monoScratch.data(), static_cast<int> (frames));

                captured.fetch_add (frames, std::memory_order_relaxed);
            }

            session.capture->ReleaseBuffer (frames);

            if (FAILED (session.capture->GetNextPacketSize (&packetFrames)))
                break;
        }
    }

    session.close();
    capturing.store (false, std::memory_order_relaxed);
}

juce::String LoopbackCapture::start (const juce::String& deviceName)
{
    stop();

    wantedDevice = deviceName;
    captured.store (0, std::memory_order_relaxed);
    startupError.clear();
    startupDone.reset();

    startThread (juce::Thread::Priority::normal);

    // La apertura ocurre en el hilo de captura a propósito: los objetos de
    // WASAPI se crean y se usan en el mismo apartamento COM, y cruzarlos de
    // hilo es pedir un fallo raro y difícil de reproducir.
    if (! startupDone.wait (3000))
    {
        stop();
        return "La escucha no arranco a tiempo."_u8;
    }

    if (! startupError.isEmpty())
    {
        stop();
        return startupError;
    }

    return {};
}

void LoopbackCapture::stop()
{
    signalThreadShouldExit();
    notify();
    stopThread (2000);

    capturing.store (false, std::memory_order_relaxed);

    const juce::ScopedLock lock (nameLock);
    openedDevice.clear();
}

juce::String LoopbackCapture::deviceName() const
{
    const juce::ScopedLock lock (nameLock);
    return openedDevice;
}

#else

LoopbackCapture::LoopbackCapture() : juce::Thread ("keyla-loopback") {}
LoopbackCapture::~LoopbackCapture() = default;

juce::String LoopbackCapture::defaultOutputLabel() { return juce::String (juce::CharPointer_UTF8 (defaultLabel)); }
juce::StringArray LoopbackCapture::availableOutputs() { return { defaultOutputLabel() }; }
juce::String LoopbackCapture::openSession (Session&, const juce::String&) { return "Solo en Windows."; }
void LoopbackCapture::run() {}
juce::String LoopbackCapture::start (const juce::String&) { return "Solo en Windows."; }
void LoopbackCapture::stop() {}
juce::String LoopbackCapture::deviceName() const { return {}; }

#endif

} // namespace keyla::app
