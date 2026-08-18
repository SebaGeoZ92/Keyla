#pragma once

#include "../AudioDeviceHost.h"
#include "../MidiInputHost.h"
#include "../SettingsStore.h"
#include "NowPlayingView.h"
#include "PianoKeyboardView.h"

#include <core/instrument/Instruments.h>

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
    void selectInstrument (core::InstrumentId id);
    void saveSettings();

    // ── Dominio de tiempo real ──────────────────────────────────────────────
    //
    // El instrumento anterior se conserva vivo un rato tras el cambio: el hilo
    // de audio puede estar todavía dentro de su process() cuando la UI ya ha
    // publicado el nuevo puntero.
    std::unique_ptr<core::IInstrument> instrument;
    std::unique_ptr<core::IInstrument> retiredInstrument;

    AudioDeviceHost audioHost;
    MidiInputHost midiHost { audioHost };

    // ── UI ──────────────────────────────────────────────────────────────────
    PianoKeyboardView keyboardView;
    NowPlayingView nowPlayingView;

    juce::ComboBox audioDeviceBox, midiDeviceBox, bufferSizeBox, instrumentBox;
    juce::ToggleButton exclusiveToggle { "Modo exclusivo (menos latencia)" };
    juce::Slider reverbSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider volumeSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label reverbLabel, volumeLabel, statusLabel, messageLabel;
    juce::TextButton panicButton { "Silencio" };
    juce::TextButton learnButton { "Aprender" };

    /** Ajustes recordados entre sesiones. Se llama `prefs` y no `settings`
        porque AudioDeviceHost::Settings ya ocupa ese nombre y confundirlos
        sería una tarde perdida. */
    Settings prefs;

    juce::String pendingMessage;
    bool messageIsError { false };
    float shownVolume { -1.0f };        // para no pelearse con el mando del teclado

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace keyla::app
