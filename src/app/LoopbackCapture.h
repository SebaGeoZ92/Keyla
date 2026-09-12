#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>

namespace keyla::app
{

/** Captura lo que está sonando por una salida de Windows.

    Esto no lo trae JUCE: en todo `juce_audio_devices` no aparece la palabra
    *loopback* ni una vez, y los únicos modos de WASAPI que expone son
    compartido, exclusivo y compartido de baja latencia. Así que hay que bajar a
    COM y hablar con `IAudioClient` directamente. Son doscientas líneas y viven
    aquí, en `src/app/`, porque son dispositivos: `core/` no puede verlas
    (invariante 6).

    Tres cosas que conviene saber antes de usarlo:

    - **Sólo funciona en modo compartido.** Un endpoint abierto en exclusivo no
      se puede escuchar: el modo exclusivo es exactamente el permiso de que
      nadie más toque ese flujo. Si Keyla está sonando en exclusivo por la misma
      salida que se quiere escuchar, una de las dos cosas tiene que ceder.
    - **Si no suena nada, no llega nada.** Windows no produce datos de loopback
      mientras ningún programa esté reproduciendo por esa salida; no es que
      lleguen ceros, es que no llega ningún paquete. Por eso se cuenta el audio
      recibido: sin ese dato, "no reconoce nada" y "no está llegando nada" se
      parecen demasiado.
    - **Se oye todo lo que salga por ahí, Keyla incluida.** Si se escucha la
      misma salida por la que Keyla toca, Keyla se oye a sí misma.

    El audio se entrega ya mezclado a mono y en punto flotante, desde el hilo de
    captura. Quien lo reciba no puede bloquear mucho rato.
*/
class LoopbackCapture final : private juce::Thread
{
public:
    LoopbackCapture();
    ~LoopbackCapture() override;

    /** Nombres de las salidas que se pueden escuchar. El primero es siempre
        "(la salida predeterminada)", que es lo que quiere el 90 % de las veces:
        es por donde suena el navegador. */
    static juce::StringArray availableOutputs();

    static juce::String defaultOutputLabel();

    /** Abre y empieza a capturar. Devuelve cadena vacía si todo fue bien, y si
        no, el motivo en castellano. Bloquea hasta un par de segundos: la
        apertura ocurre en el hilo de captura, porque COM y los objetos de
        WASAPI se llevan mal con cruzar de hilo. */
    juce::String start (const juce::String& deviceName);

    void stop();

    bool isCapturing() const noexcept { return capturing.load (std::memory_order_relaxed); }

    /** La del dispositivo capturado, que no tiene por qué ser 48 kHz. */
    double sampleRate() const noexcept { return capturedRate.load (std::memory_order_relaxed); }

    juce::String deviceName() const;

    /** Samples entregados desde que se abrió. Si esto no sube, no está sonando
        nada por esa salida — que es un diagnóstico distinto de "no lo reconoce"
        y hay que poder distinguirlos. */
    std::uint64_t samplesCaptured() const noexcept { return captured.load (std::memory_order_relaxed); }

    /** Mono. Llamado desde el hilo de captura. Se fija antes de `start`. */
    std::function<void (const float*, int)> onAudio;

private:
    void run() override;

    struct Session;
    juce::String openSession (Session& session, const juce::String& deviceName);

    juce::String wantedDevice;
    juce::String openedDevice;
    mutable juce::CriticalSection nameLock;

    juce::WaitableEvent startupDone;
    juce::String startupError;

    std::atomic<bool> capturing { false };
    std::atomic<double> capturedRate { 0.0 };
    std::atomic<std::uint64_t> captured { 0 };

    std::vector<float> monoScratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopbackCapture)
};

} // namespace keyla::app
