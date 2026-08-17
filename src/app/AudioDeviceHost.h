#pragma once

// El dominio de tiempo real de la aplicación. Es la carcasa que conecta el
// dispositivo de audio con core/, y la única pieza del programa que tiene
// permiso para hablar con JUCE audio_devices.

#include "EngineSnapshot.h"

#include <core/audio/Limiter.h>
#include <core/instrument/IInstrument.h>
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

    // ── Entrada MIDI (la llama MidiInputHost, desde el hilo del driver) ─────

    /** Empuja un mensaje con el instante en que llegó, medido con el reloj de
        alta resolución. No bloquea ni asigna: es la FIFO del invariante 4. */
    void pushMidiMessage (const core::RawMidiMessage& message, double hostSeconds) noexcept;

    /** Inyecta una nota desde la UI (teclado en pantalla). Mismo camino. */
    void pushMidiMessage (const core::RawMidiMessage& message) noexcept;

    // ── Consulta desde la UI ────────────────────────────────────────────────

    EngineSnapshot snapshot() const { return published.read(); }

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

    core::IInstrument* instrument { nullptr };
    core::Transport transport;
    core::KeyboardState keyboard;
    core::Limiter limiter;

    core::LockFreeQueue<IncomingMidi, 1024> midiFifo;
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
