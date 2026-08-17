#include "AudioDeviceHost.h"

#include <core/text/Utf8.h>

#include <algorithm>
#include <cmath>
#include <thread>

namespace keyla::app
{

using core::RawMidiMessage;
using core::StampedMidiEvent;
using keyla::operator""_u8;

namespace
{
    double nowSeconds() noexcept
    {
        return juce::Time::getMillisecondCounterHiRes() * 0.001;
    }

    std::unique_ptr<juce::AudioIODeviceType> createType (bool exclusive)
    {
       #if JUCE_WINDOWS
        const auto mode = exclusive ? juce::WASAPIDeviceMode::exclusive
                                    : juce::WASAPIDeviceMode::shared;

        return std::unique_ptr<juce::AudioIODeviceType> (
                   juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (mode));
       #else
        juce::ignoreUnused (exclusive);
        return std::unique_ptr<juce::AudioIODeviceType> (
                   juce::AudioIODeviceType::createAudioIODeviceType_CoreAudio());
       #endif
    }

    bool nameMatchesAny (const juce::String& name, std::initializer_list<const char*> hints)
    {
        for (auto* hint : hints)
            if (name.containsIgnoreCase (hint))
                return true;

        return false;
    }
}

AudioDeviceHost::AudioDeviceHost() = default;

AudioDeviceHost::~AudioDeviceHost()
{
    close();
}

// ── Dispositivos ────────────────────────────────────────────────────────────

juce::StringArray AudioDeviceHost::availableOutputs (bool exclusive)
{
    auto type = createType (exclusive);

    if (type == nullptr)
        return {};

    type->scanForDevices();
    return type->getDeviceNames (false);
}

juce::String AudioDeviceHost::deviceName() const
{
    return device != nullptr ? device->getName() : juce::String();
}

juce::Array<int> AudioDeviceHost::availableBufferSizes() const
{
    return device != nullptr ? device->getAvailableBufferSizes() : juce::Array<int>();
}

juce::Array<double> AudioDeviceHost::availableSampleRates() const
{
    return device != nullptr ? device->getAvailableSampleRates() : juce::Array<double>();
}

bool AudioDeviceHost::outputLooksBluetooth() const
{
    return nameMatchesAny (deviceName(), { "bluetooth", "bth", "airpods", "hands-free", "a2dp" });
}

bool AudioDeviceHost::nameLooksVirtual (const juce::String& name)
{
    return nameMatchesAny (name, { "virtual", "voicemeeter", "vb-audio", "vb-cable",
                                   "sonar", "cable output", "cable input", "steelseries" });
}

bool AudioDeviceHost::outputLooksWireless() const
{
    return ! outputLooksBluetooth()
        && nameMatchesAny (deviceName(), { "wireless", "inal", "2.4g", "dongle" });
}

juce::String AudioDeviceHost::open (const Settings& settings)
{
    close();

    deviceType = createType (settings.exclusive);

    if (deviceType == nullptr)
        return "No hay backend de audio disponible.";

    deviceType->scanForDevices();

    const auto outputs = deviceType->getDeviceNames (false);

    if (outputs.isEmpty())
        return "No hay ningún dispositivo de salida en "_u8 + deviceType->getTypeName() + ".";

    juce::String wanted = settings.outputDeviceName;

    if (wanted.isEmpty())
    {
        const auto defaultIndex = deviceType->getDefaultDeviceIndex (false);
        wanted = juce::isPositiveAndBelow (defaultIndex, outputs.size()) ? outputs[defaultIndex]
                                                                        : outputs[0];
    }
    else if (! outputs.contains (wanted))
    {
        return "El dispositivo \"" + wanted + "\" ya no está disponible."_u8;
    }

    device.reset (deviceType->createDevice (wanted, {}));

    if (device == nullptr)
        return "No se pudo crear el dispositivo \"" + wanted + "\".";

    // ── Formato ─────────────────────────────────────────────────────────────
    active = settings;
    active.outputDeviceName = wanted;

    const auto rates = device->getAvailableSampleRates();

    if (! rates.contains (active.sampleRate))
    {
        if (rates.isEmpty())
        {
            device.reset();
            return "El dispositivo no declara ninguna frecuencia de muestreo.";
        }

        // Se prefiere 48 kHz y, si no, la más cercana a lo pedido.
        double best = rates[0];

        for (auto rate : rates)
            if (std::abs (rate - active.sampleRate) < std::abs (best - active.sampleRate))
                best = rate;

        active.sampleRate = best;
    }

    const auto sizes = device->getAvailableBufferSizes();

    if (! sizes.contains (active.bufferSize) && ! sizes.isEmpty())
    {
        // El H510-PRO no admite 128: los 128 samples del doc 03 no son
        // universales, así que se coge el más cercano y se sigue.
        int best = sizes[0];

        for (auto size : sizes)
            if (std::abs (size - active.bufferSize) < std::abs (best - active.bufferSize))
                best = size;

        active.bufferSize = best;
    }

    const auto numOutputChannels = device->getOutputChannelNames().size();

    juce::BigInteger outputChannels;
    outputChannels.setRange (0, juce::jmin (2, numOutputChannels), true);

    auto error = device->open ({}, outputChannels, active.sampleRate, active.bufferSize);

    if (error.isNotEmpty())
    {
        // El modo exclusivo suele exigir el formato nativo: segundo intento con
        // todos los canales del dispositivo.
        outputChannels.clear();
        outputChannels.setRange (0, numOutputChannels, true);
        error = device->open ({}, outputChannels, active.sampleRate, active.bufferSize);
    }

    if (error.isNotEmpty())
    {
        device.reset();
        return active.exclusive
             ? "No se pudo abrir en modo exclusivo: " + error
               + "\nSuele significar que otra aplicación tiene el dispositivo tomado."_u8
             : "No se pudo abrir el dispositivo: " + error;
    }

    device->start (this);

    // ── Abrirse y arrancar no son lo mismo ──────────────────────────────────
    // Hay dispositivos virtuales que aceptan open(), levantan isOpen() y no
    // entregan un solo callback. Si no se comprueba aquí, la aplicación se
    // queda muda sin decir por qué.
    {
        const double deadline = nowSeconds() + 1.5;

        while (callbackCount == 0 && nowSeconds() < deadline)
            std::this_thread::sleep_for (std::chrono::milliseconds (20));

        if (callbackCount == 0)
        {
            const auto lastError = device->getLastError();
            device->stop();
            device.reset();

            return "\"" + wanted + "\" se abrió pero no entrega audio: el stream no arranca."_u8
                 + (lastError.isEmpty() ? juce::String() : "\n" + lastError)
                 + "\nEs típico de los dispositivos virtuales. Elige la salida física."_u8;
        }
    }

    active.sampleRate = device->getCurrentSampleRate();
    active.bufferSize = device->getCurrentBufferSizeSamples();
    running.store (true, std::memory_order_release);

    return {};
}

void AudioDeviceHost::close()
{
    running.store (false, std::memory_order_release);

    if (device != nullptr)
    {
        device->stop();
        device->close();
        device.reset();
    }

    deviceType.reset();
}

void AudioDeviceHost::setInstrument (core::IInstrument* newInstrument)
{
    jassert (! isRunning());        // sólo con el motor parado
    instrument = newInstrument;
}

void AudioDeviceHost::resetHealthCounters() noexcept
{
    // Lo leen y escriben hilos distintos, pero son contadores de diagnóstico:
    // un valor a medio camino durante un frame no le hace daño a nadie.
    dropouts = 0;
    cpuPeak = 0.0;
    callbackCount = 0;
}

// ── Entrada MIDI ────────────────────────────────────────────────────────────

void AudioDeviceHost::pushMidiMessage (const RawMidiMessage& message, double hostSeconds) noexcept
{
    midiFifo.push (IncomingMidi { message, hostSeconds });
}

void AudioDeviceHost::pushMidiMessage (const RawMidiMessage& message) noexcept
{
    pushMidiMessage (message, nowSeconds());
}

// ── Hilo de audio ───────────────────────────────────────────────────────────

void AudioDeviceHost::audioDeviceAboutToStart (juce::AudioIODevice* startingDevice)
{
    const auto rate = startingDevice->getCurrentSampleRate();
    const auto blockSize = startingDevice->getCurrentBufferSizeSamples();

    outputLatencySeconds = static_cast<double> (startingDevice->getOutputLatencyInSamples()) / rate;

    transport.prepare (rate);
    keyboard.reset();
    limiter.prepare (rate);

    if (instrument != nullptr)
        instrument->prepare (rate, blockSize);

    lastCallbackSeconds = 0.0;
    streamStartSeconds = nowSeconds();
    streamSettled = false;
    cpuAverage = 0.0;
    cpuPeak = 0.0;
    jitterMean = 0.0;
    jitterM2 = 0.0;
    callbackCount = 0;
    dropouts = 0;
    noteOnCount = 0;
    peakLevel = 0.0f;
}

void AudioDeviceHost::audioDeviceStopped()
{
    if (instrument != nullptr)
        instrument->reset();
}

void AudioDeviceHost::audioDeviceIOCallbackWithContext (const float* const*,
                                                        int,
                                                        float* const* outputChannelData,
                                                        int numOutputChannels,
                                                        int numSamples,
                                                        const juce::AudioIODeviceCallbackContext&)
{
    const juce::ScopedNoDenormals noDenormals;

    const double callbackStart = nowSeconds();
    const double periodSeconds = static_cast<double> (numSamples) / transport.sampleRate();

    // ── Salud del stream ────────────────────────────────────────────────────
    if (lastCallbackSeconds > 0.0)
    {
        const double delta = callbackStart - lastCallbackSeconds;

        // Los primeros callbacks de un stream exclusivo llegan irregulares
        // mientras el driver se asienta, y contar eso como dropout es una
        // mentira que sale en pantalla nada más abrir. Se ignora un cuarto de
        // segundo: lo que pase después sí es un fallo de verdad.
        if (streamSettled && delta > periodSeconds * 1.5 + 0.0005)
            ++dropouts;

        if (! streamSettled && callbackStart - streamStartSeconds > 0.25)
            streamSettled = true;

        // Welford sobre el intervalo real entre callbacks (doc 04 §5).
        const double deltaMs = delta * 1000.0;
        const double diff = deltaMs - jitterMean;
        jitterMean += diff / static_cast<double> (callbackCount + 1);
        jitterM2 += diff * (deltaMs - jitterMean);
    }

    lastCallbackSeconds = callbackStart;

    // ── Silencio de partida ─────────────────────────────────────────────────
    for (int channel = 0; channel < numOutputChannels; ++channel)
        if (outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[channel], numSamples);

    // ── Drenado de la FIFO MIDI (invariante 4) ──────────────────────────────
    const double blockStart = static_cast<double> (transport.samplePosition());
    std::size_t numEvents = 0;

    IncomingMidi incoming {};

    while (numEvents < maxEventsPerBlock && midiFifo.pop (incoming))
    {
        // doc 02 §3: al dominio de samples, conservando la parte fraccionaria.
        const double delta = incoming.hostSeconds - callbackStart;      // ≤ 0
        const double exact = blockStart + delta * transport.sampleRate();

        auto& event = eventScratch[numEvents++];
        event.message = incoming.message;
        event.exactSample = exact;
        event.renderOffset = std::clamp (static_cast<int> (std::llround (exact - blockStart)),
                                         0, numSamples - 1);

        keyboard.apply (incoming.message, static_cast<std::uint64_t> (std::llround (exact)));

        if (incoming.message.isNoteOn())
            ++noteOnCount;
    }

    // ── Síntesis ────────────────────────────────────────────────────────────
    if (instrument != nullptr && numOutputChannels > 0)
    {
        juce::AudioBuffer<float> buffer (outputChannelData, numOutputChannels, numSamples);

        instrument->process (buffer, core::MidiEventSpan { eventScratch.data(), numEvents });

        float* left = outputChannelData[0];
        float* right = numOutputChannels > 1 ? outputChannelData[1] : nullptr;

        limiter.process (left, right, numSamples);

        // Red de seguridad tras el limitador: pase lo que pase, nada sale por
        // encima de fondo de escala hacia unos auriculares.
        float blockPeak = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            left[i] = juce::jlimit (-1.0f, 1.0f, left[i]);
            blockPeak = std::max (blockPeak, std::abs (left[i]));

            if (right != nullptr)
                right[i] = juce::jlimit (-1.0f, 1.0f, right[i]);
        }

        peakLevel = std::max (blockPeak, peakLevel * 0.92f);   // caída suave del medidor

        // Los canales que no sean el par estéreo reciben copia del izquierdo.
        for (int channel = 2; channel < numOutputChannels; ++channel)
            if (outputChannelData[channel] != nullptr)
                juce::FloatVectorOperations::copy (outputChannelData[channel], left, numSamples);
    }

    transport.advance (numSamples);
    ++callbackCount;

    // ── Carga de CPU ────────────────────────────────────────────────────────
    const double elapsed = nowSeconds() - callbackStart;
    const double load = elapsed / periodSeconds;

    cpuAverage += 0.02 * (load - cpuAverage);       // media móvil suave
    cpuPeak = std::max (cpuPeak, load);

    publishSnapshot (numSamples, callbackStart);
}

void AudioDeviceHost::publishSnapshot (int numSamples, double) noexcept
{
    EngineSnapshot snapshot;

    snapshot.sampleRate = transport.sampleRate();
    snapshot.bufferSize = numSamples;
    snapshot.outputLatencyMs = outputLatencySeconds * 1000.0;
    snapshot.streamSamples = transport.samplePosition();

    snapshot.cpuMean = cpuAverage;
    snapshot.cpuPeak = cpuPeak;
    snapshot.dropouts = dropouts;
    snapshot.midiRejected = midiFifo.rejectedCount();
    snapshot.callbackJitterMs = callbackCount > 1
                              ? std::sqrt (jitterM2 / static_cast<double> (callbackCount - 1))
                              : 0.0;
    snapshot.gainReduction = limiter.currentGainReduction();
    snapshot.peakLevel = peakLevel;

    snapshot.activeVoices = instrument != nullptr ? instrument->activeVoiceCount() : 0;
    snapshot.numKeysDown = keyboard.numKeysDown();
    snapshot.sustainValue = keyboard.sustainPedalValue();
    snapshot.noteOnCount = noteOnCount;

    for (int note = 0; note < 128; ++note)
    {
        if (keyboard.isKeyDown (note))
            EngineSnapshot::setBit (snapshot.keysDown, note);

        if (keyboard.isSounding (note))
            EngineSnapshot::setBit (snapshot.sounding, note);
    }

    published.publish (snapshot);
}

} // namespace keyla::app
