#pragma once

// El dominio de tiempo real del spike (doc 02 §1).
//
// Dos hilos entran aquí y ninguno de los dos es el nuestro:
//   · el hilo del driver MIDI  → handleIncomingMidiMessage()
//   · el hilo del driver audio → audioDeviceIOCallbackWithContext()
//
// Entre ellos sólo hay una FIFO lock-free (invariante 4). Hacia la consola sólo
// salen snapshots inmutables y colas de registros (invariante 5).

#include "Lockfree.h"

#include <juce_audio_devices/juce_audio_devices.h>

namespace keyla::probe
{

// ── Lo que el hilo MIDI le pasa al hilo de audio ────────────────────────────

struct TimedMidi
{
    double hostSeconds;     // reloj de alta resolución leído en el callback MIDI
    double driverSeconds;   // sello del driver (1 ms de granularidad en MME)
    std::uint8_t bytes[3];
    std::uint8_t size;
};

// ── Lo que el hilo de audio le pasa a la consola ────────────────────────────

struct NoteRecord
{
    double arrivalHostSeconds;  // cuándo llegó, reloj de alta resolución
    double driverSeconds;       // cuándo dice el driver que llegó
    double bufferWaitMs;        // espera hasta el inicio del bloque
    double systemLatencyMs;     // bufferWait + latencia de salida (doc 04 §4)
    int pitch;
    int velocity;
};

struct LoadRecord
{
    float cpuFraction;          // duración del callback / periodo del buffer
    float callbackDeltaMs;      // intervalo real respecto al callback anterior
};

// ── Snapshot que lee la consola ─────────────────────────────────────────────

struct ProbeSnapshot
{
    double streamSeconds;
    double callbackPeriodMs;
    double cpuMean;
    double cpuMax;
    double callbackDeltaMean;
    double callbackDeltaSigma;
    double callbackDeltaMax;
    std::uint64_t callbacks;
    std::uint64_t noteOnCount;
    std::uint64_t noteOffCount;
    std::uint64_t gapDropouts;   // huecos en el reloj del stream
    std::uint64_t voiceSteals;
    std::uint64_t lateEvents;    // eventos que llegaron con el callback ya empezado
    int activeVoices;
};

// ────────────────────────────────────────────────────────────────────────────

class ProbeEngine final : public juce::AudioIODeviceCallback,
                          public juce::MidiInputCallback
{
public:
    ProbeEngine (int maxVoices, int stressVoices, double stressGain);
    ~ProbeEngine() override = default;

    // Latencia de salida a usar en el cálculo del doc 04 §4. Si es negativa se
    // toma la que declara el driver. Se fija antes de arrancar el stream.
    void setOutputLatencyOverrideMs (double ms) noexcept { outputLatencyOverrideMs = ms; }

    // ── Hilo de audio ───────────────────────────────────────────────────────
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;

    // ── Hilo MIDI ───────────────────────────────────────────────────────────
    void handleIncomingMidiMessage (juce::MidiInput* source,
                                    const juce::MidiMessage& message) override;

    // ── Consola ─────────────────────────────────────────────────────────────
    ProbeSnapshot snapshot() const { return published.read(); }

    bool popNote (NoteRecord& out) noexcept { return noteRecords.pop (out); }
    bool popLoad (LoadRecord& out) noexcept { return loadRecords.pop (out); }

    std::uint64_t midiQueueDrops() const noexcept { return midiFifo.droppedCount(); }
    std::uint64_t recordQueueDrops() const noexcept { return noteRecords.droppedCount(); }

    double outputLatencySeconds() const noexcept { return outputLatencySec; }
    double actualSampleRate() const noexcept { return sampleRate; }
    int actualBufferSize() const noexcept { return bufferSize; }

private:
    struct Voice
    {
        // Tono sostenido mientras la tecla esté pulsada, no envolvente de piano.
        // Aquí lo que hace falta es una referencia estable: si el sonido se
        // corta, que sea porque se ha cortado y no porque decae por diseño.
        enum class Stage { off, attack, sustain, release };

        Stage stage { Stage::off };
        int pitch { -1 };
        std::uint64_t order { 0 };
        double phase { 0.0 };
        double phaseInc { 0.0 };
        double envelope { 0.0 };
        double amplitude { 0.0 };
    };

    void noteOn (int pitch, int velocity) noexcept;
    void noteOff (int pitch) noexcept;
    void renderVoice (Voice& v, float* dest, int numSamples, double gain) noexcept;

    // Voces "de estrés": suenan siempre para medir la CPU a polifonía plena.
    void startStressVoices() noexcept;

    const int maxVoices;
    const int numStressVoices;
    const double stressGain;

    std::vector<Voice> voices;          // asignado antes de arrancar el stream
    std::vector<Voice> stressVoicePool;

    double sampleRate { 48000.0 };
    int bufferSize { 128 };
    double outputLatencySec { 0.0 };
    double outputLatencyOverrideMs { -1.0 };

    double attackInc { 0.0 };
    double releaseCoef { 0.0 };

    std::uint64_t streamSamplePos { 0 };
    std::uint64_t voiceCounter { 0 };
    double lastCallbackHostSeconds { 0.0 };

    RunningStats cpuStats;
    RunningStats deltaStats;
    std::uint64_t callbackCount { 0 };
    std::uint64_t noteOnCount { 0 };
    std::uint64_t noteOffCount { 0 };
    std::uint64_t gapDropouts { 0 };
    std::uint64_t voiceSteals { 0 };
    std::uint64_t lateEvents { 0 };

    SpscQueue<TimedMidi, 2048> midiFifo;
    SpscQueue<NoteRecord, 8192> noteRecords;
    SpscQueue<LoadRecord, 65536> loadRecords;
    SnapshotPublisher<ProbeSnapshot> published;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProbeEngine)
};

} // namespace keyla::probe
