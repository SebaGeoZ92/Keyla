#include "MidiPulseGenerator.h"

namespace keyla::probe
{

namespace
{
    double nowSeconds() noexcept
    {
        return juce::Time::getMillisecondCounterHiRes() * 0.001;
    }
}

MidiPulseGenerator::MidiPulseGenerator (std::unique_ptr<juce::MidiOutput> output, double periodMs)
    : juce::Thread ("keyla-midi-pulse"),
      midiOut (std::move (output)),
      periodSeconds (periodMs * 0.001)
{
}

MidiPulseGenerator::~MidiPulseGenerator()
{
    stop();
}

void MidiPulseGenerator::start()
{
    startThread (juce::Thread::Priority::highest);
}

void MidiPulseGenerator::stop()
{
    if (isThreadRunning())
        stopThread (2000);
}

void MidiPulseGenerator::waitUntil (double hostSeconds)
{
    for (;;)
    {
        if (threadShouldExit())
            return;

        const double remaining = hostSeconds - nowSeconds();

        if (remaining <= 0.0)
            return;

        if (remaining > 0.003)
            wait (1);           // dormir la mayor parte
        else
            juce::Thread::yield();   // los últimos 3 ms, girando: es lo preciso
    }
}

void MidiPulseGenerator::run()
{
    if (midiOut == nullptr)
        return;

    // Medio segundo de margen: que el stream de audio se estabilice antes de
    // empezar a medir.
    const double t0 = nowSeconds() + 0.5;
    std::uint32_t index = 0;

    while (! threadShouldExit())
    {
        const double intended = t0 + static_cast<double> (index) * periodSeconds;
        const int pitch = 48 + static_cast<int> (index % 25);

        waitUntil (intended);

        if (threadShouldExit())
            break;

        const double actual = nowSeconds();
        midiOut->sendMessageNow (juce::MidiMessage::noteOn (1, pitch, static_cast<juce::uint8> (100)));

        PulseRecord rec {};
        rec.intendedSeconds = intended;
        rec.actualSeconds = actual;
        rec.pitch = pitch;
        rec.index = index;
        pulses.push (rec);

        sent.fetch_add (1, std::memory_order_relaxed);

        waitUntil (intended + periodSeconds * 0.5);

        if (threadShouldExit())
            break;

        midiOut->sendMessageNow (juce::MidiMessage::noteOff (1, pitch));

        ++index;
    }

    // Que no quede nada sonando.
    midiOut->sendMessageNow (juce::MidiMessage::allNotesOff (1));
}

} // namespace keyla::probe
