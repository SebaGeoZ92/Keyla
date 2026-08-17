#include "MainComponent.h"

#include <core/text/Utf8.h>

namespace keyla::app
{

using keyla::operator""_u8;

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
    instrument = core::createInstrument (core::InstrumentId::piano);
    audioHost.setInstrument (instrument.get());
    audioHost.setReverbMix (core::defaultReverbFor (core::InstrumentId::piano));

    // ── Controles ───────────────────────────────────────────────────────────
    addAndMakeVisible (audioDeviceBox);
    addAndMakeVisible (midiDeviceBox);
    addAndMakeVisible (bufferSizeBox);
    addAndMakeVisible (exclusiveToggle);
    addAndMakeVisible (instrumentBox);
    addAndMakeVisible (reverbSlider);
    addAndMakeVisible (reverbLabel);

    for (auto id : { core::InstrumentId::piano, core::InstrumentId::electricPiano,
                     core::InstrumentId::organ, core::InstrumentId::accordion,
                     core::InstrumentId::strings, core::InstrumentId::vibraphone })
        instrumentBox.addItem (core::instrumentName (id), static_cast<int> (id) + 1);

    instrumentBox.setSelectedId (static_cast<int> (core::InstrumentId::piano) + 1,
                                 juce::dontSendNotification);

    instrumentBox.onChange = [this]
    {
        selectInstrument (static_cast<core::InstrumentId> (instrumentBox.getSelectedId() - 1));
    };

    reverbLabel.setText ("Sala", juce::dontSendNotification);
    reverbLabel.setFont (juce::FontOptions (13.0f));
    reverbLabel.setColour (juce::Label::textColourId, juce::Colour { 0xff9aa2ad });

    reverbSlider.setRange (0.0, 1.0, 0.01);
    reverbSlider.setValue (core::defaultReverbFor (core::InstrumentId::piano),
                           juce::dontSendNotification);
    reverbSlider.onValueChange = [this]
    {
        audioHost.setReverbMix (static_cast<float> (reverbSlider.getValue()));
    };
    addAndMakeVisible (keyboardView);
    addAndMakeVisible (nowPlayingView);
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
                         : "Teclado desconectado. Se reconectará solo al volver a enchufarlo."_u8,
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
    audioHost.close();          // para el stream antes de soltar el instrumento
    audioHost.setInstrument (nullptr);
    instrument.reset();
    retiredInstrument.reset();
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
    int index = outputs.indexOf (previous);

    if (index < 0)
    {
        // Sin elección previa se prefiere una salida física. El predeterminado
        // de Windows aquí es un mezclador virtual, y arrancar sobre él significa
        // medir latencias que no son las del camino real hasta el oído.
        index = 0;

        for (int i = 0; i < outputs.size(); ++i)
        {
            if (! AudioDeviceHost::nameLooksVirtual (outputs[i]))
            {
                index = i;
                break;
            }
        }
    }

    audioDeviceBox.setSelectedItemIndex (index, juce::dontSendNotification);
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
        note = "  ·  AVISO: salida Bluetooth, 100-300 ms. Con esto no se puede tocar."_u8;
    else if (audioHost.outputLooksVirtual())
        note = "  ·  AVISO: es un mezclador virtual, no tu tarjeta. Añade latencia que no se"
               " declara aquí. Elige la salida física."_u8;
    else if (audioHost.outputLooksWireless())
        note = "  ·  Salida inalámbrica: la latencia declarada puede quedarse corta."_u8;

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

void MainComponent::selectInstrument (core::InstrumentId id)
{
    auto replacement = core::createInstrument (id);

    // El anterior se retira, no se destruye: el hilo de audio puede estar
    // todavía dentro de su process() cuando aquí se publica el puntero nuevo.
    // Se libera en el siguiente cambio, que llegará como pronto dentro de un
    // buen puñado de bloques.
    retiredInstrument = std::move (instrument);
    instrument = std::move (replacement);

    audioHost.swapInstrument (instrument.get());

    reverbSlider.setValue (core::defaultReverbFor (id), juce::sendNotificationSync);

    showMessage (core::instrumentName (id), false);
}

// ── Bucle de UI: polling, nunca notificaciones desde RT (invariante 5) ──────

void MainComponent::timerCallback()
{
    const auto snapshot = audioHost.snapshot();

    keyboardView.updateFrom (snapshot);
    nowPlayingView.updateFrom (snapshot);

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

    const auto separator = "   ·   "_u8;

    juce::String status;
    status << juce::String (snapshot.sampleRate / 1000.0, 1) << " kHz"
           << separator << "buffer " << snapshot.bufferSize
           << " (" << juce::String (1000.0 * snapshot.bufferSize / snapshot.sampleRate, 2) << " ms)"
           << separator << "latencia de salida " << juce::String (snapshot.outputLatencyMs, 1) << " ms"
           << separator << "dropouts " << juce::String (snapshot.dropouts)
           << separator << "CPU " << juce::String (snapshot.cpuMean * 100.0, 1) << " %"
           << separator << "jitter " << juce::String (snapshot.callbackJitterMs, 2) << " ms"
           << separator << "voces " << juce::String (snapshot.activeVoices);

    if (snapshot.sustainValue >= core::sustainPedalThreshold)
        status << separator << "PEDAL";

    if (snapshot.midiRejected > 0)
        status << separator << "MIDI PERDIDO " << juce::String (snapshot.midiRejected);

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
    secondRow.removeFromLeft (20);
    instrumentBox.setBounds (secondRow.removeFromLeft (180));
    secondRow.removeFromLeft (12);
    reverbLabel.setBounds (secondRow.removeFromLeft (40));
    reverbSlider.setBounds (secondRow.removeFromLeft (140));

    area.removeFromTop (10);
    messageLabel.setBounds (area.removeFromTop (22));

    area.removeFromTop (6);
    nowPlayingView.setBounds (area.removeFromTop (40));
    area.removeFromTop (6);

    statusLabel.setBounds (area.removeFromBottom (22));
    area.removeFromBottom (8);

    keyboardView.setBounds (area);
}

} // namespace keyla::app
