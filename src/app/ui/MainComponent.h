#pragma once

#include "../AudioDeviceHost.h"
#include "../MidiInputHost.h"
#include "NowPlayingView.h"
#include "PianoKeyboardView.h"

#include <core/instrument/StruckStringSynth.h>

#include <juce_gui_basics/juce_gui_basics.h>

namespace keyla::app
{

/** La ventana. Fase 1: tocar y ver qué tocas, nada más.

    Regla del doc 02 §7: durante la ejecución, como máximo dos elementos vivos
    en pantalla. Aquí son el teclado y una barra de estado. Los números técnicos
    (latencia, dropouts, CPU) van en esa barra y no en un panel — el panel de
    salud completo del doc 04 §7 llegará detrás de un botón.
*/
class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    void rebuildAudioDeviceList();
    void rebuildMidiDeviceList();
    void openSelectedAudioDevice();
    void showMessage (const juce::String& text, bool isError);
    void sendNote (int note, int velocity, bool on);

    // ── Dominio de tiempo real ──────────────────────────────────────────────
    core::StruckStringSynth synth { 32 };
    AudioDeviceHost audioHost;
    MidiInputHost midiHost { audioHost };

    // ── UI ──────────────────────────────────────────────────────────────────
    PianoKeyboardView keyboardView;
    NowPlayingView nowPlayingView;

    juce::ComboBox audioDeviceBox, midiDeviceBox, bufferSizeBox;
    juce::ToggleButton exclusiveToggle { "Modo exclusivo (menos latencia)" };
    juce::Label statusLabel, messageLabel;
    juce::TextButton panicButton { "Silencio" };

    juce::String pendingMessage;
    bool messageIsError { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace keyla::app
