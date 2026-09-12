#include "MidiInputHost.h"

namespace keyla::app
{

namespace
{
    constexpr int reconnectPollMs = 1000;
}

MidiInputHost::MidiInputHost (AudioDeviceHost& audioHost)
    : host (audioHost)
{
}

MidiInputHost::~MidiInputHost()
{
    stopTimer();
    disconnect();
}

juce::StringArray MidiInputHost::availableInputs()
{
    juce::StringArray names;

    for (const auto& info : juce::MidiInput::getAvailableDevices())
        names.add (info.name);

    return names;
}

void MidiInputHost::useDevice (const juce::String& deviceName)
{
    if (deviceName == wantedName && (deviceName.isEmpty() || isConnected()))
        return;

    disconnect();
    wantedName = deviceName;

    if (wantedName.isEmpty())
    {
        stopTimer();

        if (onConnectionChanged != nullptr)
            onConnectionChanged();

        return;
    }

    tryConnect();
    startTimer (reconnectPollMs);
}

void MidiInputHost::timerCallback()
{
    if (wantedName.isEmpty())
        return;

    const auto present = availableInputs().contains (wantedName);

    if (isConnected() && ! present)
    {
        // Desenchufado. Se cierra para no quedarse con un handle muerto.
        disconnect();

        if (onConnectionChanged != nullptr)
            onConnectionChanged();
    }
    else if (! isConnected() && present)
    {
        tryConnect();

        if (isConnected() && onConnectionChanged != nullptr)
            onConnectionChanged();
    }
}

void MidiInputHost::tryConnect()
{
    for (const auto& info : juce::MidiInput::getAvailableDevices())
    {
        if (info.name != wantedName)
            continue;

        midiInput = juce::MidiInput::openDevice (info.identifier, this);

        if (midiInput != nullptr)
            midiInput->start();

        return;
    }
}

void MidiInputHost::disconnect()
{
    if (midiInput != nullptr)
    {
        midiInput->stop();
        midiInput.reset();
    }
}

// ── Hilo del driver MIDI ────────────────────────────────────────────────────

void MidiInputHost::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    const auto size = message.getRawDataSize();

    if (size < 1 || size > 3)
        return;             // sysex y tiempo real: no se usan todavía

    core::RawMidiMessage raw;
    raw.size = static_cast<std::uint8_t> (size);

    const auto* bytes = message.getRawData();

    for (int i = 0; i < size; ++i)
        raw.bytes[i] = bytes[i];

    // El sello temporal se toma aquí y no del driver: el timestamp de MMSystem
    // tiene granularidad de 1 ms, y aquí se quiere sub-milisegundo (doc 02 §3).
    const double seconds = juce::Time::getMillisecondCounterHiRes() * 0.001;

    host.pushMidiMessage (raw, seconds);

    if (onNoteObserved != nullptr && (message.isNoteOn() || message.isNoteOff()))
        onNoteObserved (message.getNoteNumber(), message.isNoteOn(), seconds);
}

} // namespace keyla::app
