#include "MainComponent.h"

namespace keyla::app
{

namespace
{
    core::RawMidiMessage makeNoteOn (int note, int velocity)
    {
        core::RawMidiMessage message;
        message.bytes[0] = 0x90;
        message.bytes[1] = static_cast<std::uint8_t> (juce::jlimit (0, 127, note));
        message.bytes[2] = static_cast<std::uint8_t> (juce::jlimit (1, 127, velocity));
        message.size = 3;
        return message;
    }

    core::RawMidiMessage makeNoteOff (int note)
    {
        core::RawMidiMessage message;
        message.bytes[0] = 0x80;
        message.bytes[1] = static_cast<std::uint8_t> (juce::jlimit (0, 127, note));
        message.bytes[2] = 0;
        message.size = 3;
        return message;
    }

    core::RawMidiMessage makeAllNotesOff()
    {
        core::RawMidiMessage message;
        message.bytes[0] = 0xB0;
        message.bytes[1] = 123;
        message.bytes[2] = 0;
        message.size = 3;
        return message;
    }
}

MainComponent::MainComponent()
{
    audioHost.setInstrument (&synth);

    // ── Controles ───────────────────────────────────────────────────────────
    addAndMakeVisible (audioDeviceBox);
    addAndMakeVisible (midiDeviceBox);
    addAndMakeVisible (bufferSizeBox);
    addAndMakeVisible (exclusiveToggle);
    addAndMakeVisible (keyboardView);
    addAndMakeVisible (statusLabel);
    addAndMakeVisible (messageLabel);
    addAndMakeVisible (panicButton);

    exclusiveToggle.setToggleState (true, juce::dontSendNotification);
    exclusiveToggle.onClick = [this]
    {
        rebuildAudioDeviceList();
        openSelectedAudioDevice();
    };

    audioDeviceBox.onChange = [this] { openSelectedAudioDevice(); };
    bufferSizeBox.onChange = [this] { openSelectedAudioDevice(); };

    midiDeviceBox.onChange = [this]
    {
        const auto name = midiDeviceBox.getText();
        midiHost.useDevice (name == "(ninguna)" ? juce::String() : name);
    };

    midiHost.onConnectionChanged = [this]
    {
        showMessage (midiHost.isConnected()
                         ? "Teclado conectado: " + midiHost.wantedDeviceName()
                         : "Teclado desconectado. Se reconectará solo al volver a enchufarlo.",
                     ! midiHost.isConnected());
    };

    panicButton.onClick = [this] { audioHost.pushMidiMessage (makeAllNotesOff()); };

    keyboardView.setRange (36, 84);
    keyboardView.onNoteOn = [this] (int note, int velocity) { sendNote (note, velocity, true); };
    keyboardView.onNoteOff = [this] (int note) { sendNote (note, 0, false); };

    statusLabel.setFont (juce::FontOptions (13.0f));
    statusLabel.setColour (juce::Label::textColourId, juce::Colour { 0xff9aa2ad });
    messageLabel.setFont (juce::FontOptions (13.0f));

    // ── Arranque ────────────────────────────────────────────────────────────
    rebuildAudioDeviceList();
    rebuildMidiDeviceList();
    openSelectedAudioDevice();

    setSize (1000, 420);
    startTimerHz (60);
}

MainComponent::~MainComponent()
{
    stopTimer();
    midiHost.useDevice ({});
    audioHost.close();
    audioHost.setInstrument (nullptr);
}

// ── Dispositivos ────────────────────────────────────────────────────────────

void MainComponent::rebuildAudioDeviceList()
{
    const auto previous = audioDeviceBox.getText();
    const auto exclusive = exclusiveToggle.getToggleState();

    audioDeviceBox.clear (juce::dontSendNotification);

    const auto outputs = AudioDeviceHost::availableOutputs (exclusive);

    for (int i = 0; i < outputs.size(); ++i)
        audioDeviceBox.addItem (outputs[i], i + 1);

    // Se recuerda por nombre, no por índice: la lista se reordena sola cuando
    // Windows cambia el dispositivo predeterminado (doc 01 §1.1).
    const auto index = outputs.indexOf (previous);
    audioDeviceBox.setSelectedItemIndex (index >= 0 ? index : 0, juce::dontSendNotification);
}

void MainComponent::rebuildMidiDeviceList()
{
    const auto previous = midiDeviceBox.getText();

    midiDeviceBox.clear (juce::dontSendNotification);
    midiDeviceBox.addItem ("(ninguna)", 1);

    const auto inputs = MidiInputHost::availableInputs();

    for (int i = 0; i < inputs.size(); ++i)
        midiDeviceBox.addItem (inputs[i], i + 2);

    int wanted = inputs.indexOf (previous) + 2;

    if (wanted < 2)
    {
        // Si sólo hay un puerto, se coge sin preguntar. Si hay varios, se
        // prefiere el que no sea virtual: por un loopMIDI no toca nadie.
        wanted = 1;

        for (int i = 0; i < inputs.size(); ++i)
        {
            if (! inputs[i].containsIgnoreCase ("loopmidi"))
            {
                wanted = i + 2;
                break;
            }
        }
    }

    midiDeviceBox.setSelectedId (wanted, juce::sendNotificationSync);
}

void MainComponent::openSelectedAudioDevice()
{
    audioHost.close();

    AudioDeviceHost::Settings settings;
    settings.outputDeviceName = audioDeviceBox.getText();
    settings.exclusive = exclusiveToggle.getToggleState();
    settings.sampleRate = 48000.0;
    settings.bufferSize = bufferSizeBox.getSelectedId() > 0 ? bufferSizeBox.getSelectedId() : 128;

    const auto error = audioHost.open (settings);

    if (error.isNotEmpty())
    {
        showMessage (error, true);
        bufferSizeBox.clear (juce::dontSendNotification);
        return;
    }

    // Los tamaños de buffer sólo se conocen con el dispositivo abierto.
    const auto current = audioHost.currentSettings();
    const auto sizes = audioHost.availableBufferSizes();

    bufferSizeBox.clear (juce::dontSendNotification);

    for (auto size : sizes)
        bufferSizeBox.addItem (juce::String (size) + " samples  ("
                                   + juce::String (1000.0 * size / current.sampleRate, 2) + " ms)",
                               size);

    bufferSizeBox.setSelectedId (current.bufferSize, juce::dontSendNotification);

    juce::String note;

    if (audioHost.outputLooksBluetooth())
        note = "  ·  AVISO: salida Bluetooth, 100-300 ms. Con esto no se puede tocar.";
    else if (audioHost.outputLooksWireless())
        note = "  ·  Salida inalámbrica: la latencia declarada puede quedarse corta.";

    showMessage ("Sonando por " + audioHost.deviceName() + note,
                 audioHost.outputLooksBluetooth());
}

void MainComponent::showMessage (const juce::String& text, bool isError)
{
    pendingMessage = text;
    messageIsError = isError;
}

void MainComponent::sendNote (int note, int velocity, bool on)
{
    audioHost.pushMidiMessage (on ? makeNoteOn (note, velocity) : makeNoteOff (note));
}

// ── Bucle de UI: polling, nunca notificaciones desde RT (invariante 5) ──────

void MainComponent::timerCallback()
{
    const auto snapshot = audioHost.snapshot();

    keyboardView.updateFrom (snapshot);

    if (pendingMessage.isNotEmpty())
    {
        messageLabel.setText (pendingMessage, juce::dontSendNotification);
        messageLabel.setColour (juce::Label::textColourId,
                                messageIsError ? juce::Colour { 0xffe4785e }
                                               : juce::Colour { 0xff7fb069 });
        pendingMessage.clear();
    }

    if (! audioHost.isRunning())
    {
        statusLabel.setText ("sin audio", juce::dontSendNotification);
        return;
    }

    juce::String status;
    status << juce::String (snapshot.sampleRate / 1000.0, 1) << " kHz"
           << "   ·   buffer " << snapshot.bufferSize
           << " (" << juce::String (1000.0 * snapshot.bufferSize / snapshot.sampleRate, 2) << " ms)"
           << "   ·   latencia de salida " << juce::String (snapshot.outputLatencyMs, 1) << " ms"
           << "   ·   dropouts " << juce::String (snapshot.dropouts)
           << "   ·   CPU " << juce::String (snapshot.cpuMean * 100.0, 1) << " %"
           << "   ·   jitter " << juce::String (snapshot.callbackJitterMs, 2) << " ms"
           << "   ·   voces " << juce::String (snapshot.activeVoices);

    if (snapshot.sustainValue >= core::sustainPedalThreshold)
        status << "   ·   PEDAL";

    if (snapshot.midiRejected > 0)
        status << "   ·   MIDI PERDIDO " << juce::String (snapshot.midiRejected);

    statusLabel.setText (status, juce::dontSendNotification);
}

// ── Pintado y disposición ───────────────────────────────────────────────────

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour { 0xff1d1f24 });
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto topRow = area.removeFromTop (28);
    audioDeviceBox.setBounds (topRow.removeFromLeft (330));
    topRow.removeFromLeft (8);
    bufferSizeBox.setBounds (topRow.removeFromLeft (180));
    topRow.removeFromLeft (8);
    exclusiveToggle.setBounds (topRow.removeFromLeft (240));

    area.removeFromTop (8);

    auto secondRow = area.removeFromTop (28);
    midiDeviceBox.setBounds (secondRow.removeFromLeft (330));
    secondRow.removeFromLeft (8);
    panicButton.setBounds (secondRow.removeFromLeft (100));

    area.removeFromTop (10);
    messageLabel.setBounds (area.removeFromTop (22));

    statusLabel.setBounds (area.removeFromBottom (22));
    area.removeFromBottom (8);

    keyboardView.setBounds (area);
}

} // namespace keyla::app
