#pragma once

// Generador de notas para --selftest.
//
// Manda Note On a intervalos exactos por un puerto MIDI de salida (loopMIDI) y
// apunta CUÁNDO los mandó de verdad. El jitter de entrada MIDI que medimos es
// entonces σ(llegada − envío real), sin contaminarse con el jitter del propio
// hilo emisor.
//
// Ojo con la interpretación: por un puerto virtual esto mide la ruta software.
// El jitter real del SE49 lo domina el bus USB y sólo se ve con el teclado
// enchufado, en modo en vivo.

#include "Lockfree.h"

#include <juce_audio_devices/juce_audio_devices.h>

namespace keyla::probe
{

struct PulseRecord
{
    double intendedSeconds;   // cuándo tocaba mandarlo
    double actualSeconds;     // cuándo se mandó
    int pitch;
    std::uint32_t index;
};

class MidiPulseGenerator final : private juce::Thread
{
public:
    MidiPulseGenerator (std::unique_ptr<juce::MidiOutput> output, double periodMs);
    ~MidiPulseGenerator() override;

    void start();
    void stop();

    bool popPulse (PulseRecord& out) noexcept { return pulses.pop (out); }

    std::uint32_t sentCount() const noexcept { return sent.load (std::memory_order_relaxed); }

private:
    void run() override;
    void waitUntil (double hostSeconds);

    std::unique_ptr<juce::MidiOutput> midiOut;
    const double periodSeconds;

    SpscQueue<PulseRecord, 8192> pulses;
    std::atomic<std::uint32_t> sent { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiPulseGenerator)
};

} // namespace keyla::probe
