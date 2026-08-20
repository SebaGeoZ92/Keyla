#pragma once

// El dominio de tiempo real de la aplicación. Es la carcasa que conecta el
// dispositivo de audio con core/, y la única pieza del programa que tiene
// permiso para hablar con JUCE audio_devices.

#include "EngineSnapshot.h"

#include <core/audio/Limiter.h>
#include <core/instrument/IInstrument.h>
#include <core/instrument/Metronome.h>
#include <core/midi/KeyboardState.h>
#include <core/midi/LockFreeQueue.h>
#include <core/time/Transport.h>

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <memory>

namespace keyla::app
{

/** Abre el dispositivo, drena el MIDI y hace sonar el instrumento.

    Todo lo que sabe de latencia y de dropouts está medido, no supuesto: es lo
    que `audio_probe` verificó en la fase 0 sobre este mismo hardware, traído
    aquí detrás de las interfaces de `core/`.
*/
class AudioDeviceHost final : public juce::AudioIODeviceCallback
{
public:
    AudioDeviceHost();
    ~AudioDeviceHost() override;

    struct Settings
    {
        juce::String outputDeviceName;      // vacío = el predeterminado
        double sampleRate { 48000.0 };
        int bufferSize { 128 };
        bool exclusive { true };            // WASAPI Exclusive por defecto
    };

    // ── Dispositivos ────────────────────────────────────────────────────────

    /** Nombres de salida disponibles en el modo pedido. Vuelve a escanear. */
    static juce::StringArray availableOutputs (bool exclusive);

    /** Abre y arranca. Devuelve "" si todo fue bien, o el motivo del fallo.

        Comprueba de verdad que el stream arranca: hay dispositivos virtuales
        que aceptan `open()`, levantan el flag de abierto y no entregan ni un
        callback. Eso costó una tarde en la fase 0 y aquí no puede volver a ser
        un fallo silencioso. */
    juce::String open (const Settings& settings);

    void close();

    bool isRunning() const noexcept { return running.load (std::memory_order_acquire); }

    Settings currentSettings() const { return active; }
    juce::String deviceName() const;
    juce::Array<int> availableBufferSizes() const;
    juce::Array<double> availableSampleRates() const;

    /** True si el nombre del dispositivo sugiere una salida inalámbrica: son
        de 15 a 300 ms que el driver no siempre declara (doc 01 §2.1). */
    bool outputLooksWireless() const;
    bool outputLooksBluetooth() const;

    /** True si el nombre delata un mezclador por software (SteelSeries Sonar,
        Voicemeeter, VB-Cable...). No son un fallo —algunos funcionan— pero
        añaden una capa de proceso y latencia que el driver no declara, así que
        elegir uno sin querer falsea cualquier medida. */
    static bool nameLooksVirtual (const juce::String& name);
    bool outputLooksVirtual() const { return nameLooksVirtual (deviceName()); }

    // ── Instrumento ─────────────────────────────────────────────────────────

    /** Con el stream parado. El host no toma posesión. */
    void setInstrument (core::IInstrument* instrument);

    /** Cambia el instrumento **con el stream corriendo**. El puntero nuevo se
        publica de forma atómica y el callback lo recoge en el siguiente bloque;
        el anterior se silencia antes. Quien llama es responsable de mantener
        vivo el objeto viejo hasta que haya pasado un bloque. */
    void swapInstrument (core::IInstrument* instrument) noexcept;

    // ── Reverberación ───────────────────────────────────────────────────────
    //
    // El doc 02 §2 la dejó fuera de la fase 1 a propósito: "añade latencia
    // percibida y enmascara problemas de sonido". Lo primero no aplica —esta
    // no tiene retardo de entrada— pero lo segundo sí, así que llega ahora que
    // el instrumento seco ya se ha juzgado, y no antes.

    /** 0 = seco, 1 = todo reverberación. Se puede llamar mientras suena. */
    void setReverbMix (float mix) noexcept;
    float reverbMix() const noexcept { return targetReverbMix.load (std::memory_order_relaxed); }

    // ── Volumen general ─────────────────────────────────────────────────────
    //
    // Va **después** del limitador a propósito. Si fuese antes, bajar el volumen
    // dejaría de limitar y el instrumento cambiaría de carácter al bajarlo; y al
    // revés, subirlo haría saltar el recorte. Con el limitador delante, la
    // protección auditiva es la misma a cualquier volumen.

    /** 0..1. La mueven el mando de la ventana y el control del teclado. */
    void setMasterVolume (float volume) noexcept;
    float masterVolume() const noexcept { return targetVolume.load (std::memory_order_relaxed); }

    /** Número de CC que el teclado usa como volumen. 7 es el estándar, pero no
        todos los controladores lo respetan; por eso el snapshot publica el
        último CC recibido y esto se puede reasignar sin recompilar. */
    void setVolumeControllerNumber (int cc) noexcept
    {
        volumeController.store (juce::jlimit (0, 127, cc), std::memory_order_relaxed);
    }

    int volumeControllerNumber() const noexcept
    {
        return volumeController.load (std::memory_order_relaxed);
    }

    /** Aprende el mando: el siguiente control continuo que llegue pasa a ser el
        de volumen. Es la única forma honesta de que esto funcione con cualquier
        teclado — CC7 es el estándar y hay controladores que lo ignoran. */
    void learnVolumeController() noexcept { learningVolume.store (true, std::memory_order_relaxed); }
    void cancelLearn() noexcept { learningVolume.store (false, std::memory_order_relaxed); }
    bool isLearningVolumeController() const noexcept { return learningVolume.load (std::memory_order_relaxed); }

    // ── Metrónomo (doc 05, fase 2) ──────────────────────────────────────────

    void setMetronomeEnabled (bool enabled) noexcept;
    bool isMetronomeEnabled() const noexcept { return metronomeOn.load (std::memory_order_relaxed); }

    void setMetronomeGain (float gain) noexcept;

    /** Negras por minuto. Se aplica al principio del siguiente bloque, nunca a
        mitad de uno: cambiar la rejilla dentro de un bloque movería los pulsos
        ya emitidos. */
    void setTempo (double beatsPerMinute) noexcept;
    double tempo() const noexcept { return targetTempo.load (std::memory_order_relaxed); }

    /** Pone el pulso cero aquí y ahora, para que el ejercicio empiece a tiempo
        en vez de en un sitio arbitrario de la rejilla. */
    void restartBarGrid() noexcept { restartGrid.store (true, std::memory_order_relaxed); }

    // ── Entrada MIDI (la llama MidiInputHost, desde el hilo del driver) ─────

    /** Empuja un mensaje con el instante en que llegó, medido con el reloj de
        alta resolución. No bloquea ni asigna: es la FIFO del invariante 4. */
    void pushMidiMessage (const core::RawMidiMessage& message, double hostSeconds) noexcept;

    /** Inyecta una nota desde la UI (teclado en pantalla). Mismo camino. */
    void pushMidiMessage (const core::RawMidiMessage& message) noexcept;

    // ── Consulta desde la UI ────────────────────────────────────────────────

    EngineSnapshot snapshot() const { return published.read(); }

    /** Un ataque o una suelta, con su posición exacta de sample.

        El ejercicio necesita **eventos**, no un muestreo del estado a 60 Hz:
        repetir la misma nota dos veces rápido es indistinguible de mantenerla
        si sólo se mira quién está pulsado, y en una escala eso pasa cada dos
        por tres. Por eso hay una FIFO del dominio de tiempo real al de sesión,
        que es la otra dirección del invariante 4. */
    struct NoteEvent
    {
        int pitch { 0 };
        int velocity { 0 };
        bool isOn { false };
        double exactSample { 0.0 };
    };

    bool popNoteEvent (NoteEvent& out) noexcept { return noteEvents.pop (out); }

    void resetHealthCounters() noexcept;

    // ── juce::AudioIODeviceCallback ─────────────────────────────────────────

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;

private:
    struct IncomingMidi
    {
        core::RawMidiMessage message;
        double hostSeconds;
    };

    static constexpr std::size_t maxEventsPerBlock = 512;

    void publishSnapshot (int numSamples, double callbackSeconds) noexcept;

    std::unique_ptr<juce::AudioIODeviceType> deviceType;
    std::unique_ptr<juce::AudioIODevice> device;
    Settings active;

    // Atómico porque la UI puede cambiar de instrumento mientras suena.
    std::atomic<core::IInstrument*> instrument { nullptr };

    core::Transport transport;
    core::KeyboardState keyboard;
    core::Limiter limiter;
    core::Metronome metronome;

    std::atomic<bool> metronomeOn { false };
    std::atomic<float> metronomeGain { 0.5f };
    std::atomic<double> targetTempo { 90.0 };
    std::atomic<bool> restartGrid { false };

    juce::Reverb reverb;
    juce::AudioBuffer<float> reverbScratch;
    std::atomic<float> targetReverbMix { 0.0f };
    float currentReverbMix { 0.0f };

    std::atomic<float> targetVolume { 0.8f };
    std::atomic<int> volumeController { 7 };        // CC7 = Volume, el estándar
    float currentVolume { 0.8f };
    std::atomic<int> lastController { -1 };
    std::atomic<int> lastControllerValue { 0 };
    std::atomic<bool> learningVolume { false };

    core::LockFreeQueue<IncomingMidi, 1024> midiFifo;
    core::LockFreeQueue<NoteEvent, 1024> noteEvents;
    std::array<core::StampedMidiEvent, maxEventsPerBlock> eventScratch {};

    core::SnapshotPublisher<EngineSnapshot> published;

    std::atomic<bool> running { false };

    // Sólo del hilo de audio.
    double outputLatencySeconds { 0.0 };
    double lastCallbackSeconds { 0.0 };
    double streamStartSeconds { 0.0 };
    bool streamSettled { false };
    double cpuAverage { 0.0 };
    double cpuPeak { 0.0 };
    double jitterMean { 0.0 };
    double jitterM2 { 0.0 };
    std::uint64_t callbackCount { 0 };
    std::uint64_t dropouts { 0 };
    std::uint64_t noteOnCount { 0 };
    float peakLevel { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioDeviceHost)
};

} // namespace keyla::app
