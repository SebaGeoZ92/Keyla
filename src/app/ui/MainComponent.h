#pragma once

#include "../AudioDeviceHost.h"
#include "../MidiInputHost.h"
#include "../ListeningEngine.h"
#include "../SettingsStore.h"
#include "ExerciseView.h"
#include "ListeningView.h"
#include "NowPlayingView.h"
#include "PianoKeyboardView.h"

#include <core/evaluation/Metrics.h>
#include <core/evaluation/OfflineAligner.h>
#include <core/exercise/ExerciseRunner.h>
#include <core/recording/SessionRecorder.h>
#include <core/instrument/Instruments.h>
#include <core/score/ExerciseGenerator.h>
#include <core/score/MidiFileImporter.h>
#include <core/score/ProgressionGenerator.h>

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
    /** `unattended` = la arrancó Windows, no el usuario. Cambia el arranque
        entero: ver `updateUnattendedState()`. */
    explicit MainComponent (bool unattended = false);
    ~MainComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** La ventana se muestra o se aparta sola en modo desatendido. La decisión
        es de aquí —es quien sabe si hay teclado— y la ejecuta la ventana. */
    std::function<void()> onWakeRequested;
    std::function<void()> onSleepRequested;

private:
    void timerCallback() override;

    void rebuildAudioDeviceList();
    void rebuildMidiDeviceList();
    void openSelectedAudioDevice();
    void showMessage (const juce::String& text, bool isError);
    void sendNote (int note, int velocity, bool on);
    void selectInstrument (core::InstrumentId id);
    void saveSettings();
    void rebuildVariantBox();
    core::Exercise buildSelectedExercise();
    void startSelectedExercise();
    void stopExercise();
    void pumpNoteEvents();
    void finishTempoAttempt();
    void importMidiExercise();
    void rebuildListenDeviceList();
    void applyListening();
    void updateUnattendedState();
    void setOpenWithWindows (bool shouldOpen);

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

    /** La escucha del ordenador. Vive aquí y no en AudioDeviceHost porque no
        tiene nada que ver con el instrumento: es otra entrada, otro hilo y otro
        ciclo de vida. */
    ListeningEngine listening;
    ListeningView listeningView;
    ExerciseView exerciseView;

    // El profesor vive en el dominio de sesión: se alimenta de la FIFO de
    // eventos del hilo de audio, nunca al revés (doc 02 §1).
    core::ExerciseRunner runner;

    /** Sólo los ejercicios importados de MIDI se guardan en una lista. Las
        escalas, los arpegios y las progresiones se **generan al empezar** a
        partir de lo que digan los desplegables: con doce tónicas, trece
        escalas, dos manos y cuatro octavas, tenerlos precocinados en una lista
        serían más de mil entradas ilegibles. */
    std::vector<core::Exercise> importedExercises;

    /** Modo tempo: el reloj no espera. Se graba todo y al terminar se evalúa
        de una vez sobre la grabación, que es lo que permite que la evaluación
        sea una funcion pura y testeable (doc 01 1.8). El modo espera y el modo
        tempo son **dos evaluadores distintos** y mezclarlos produce el error de
        reportar errores de ritmo donde el ritmo no existe (doc 02 5). */
    core::SessionRecorder recorder;
    core::Exercise tempoExercise;
    bool tempoMode { false };
    double tempoStartSample { 0.0 };

    /** Rango del teclado en pantalla, que es también el que se supone al
        alumno. El SE49 son estas 49 teclas. Cuando exista la calibración de
        controlador, esto lo aprenderá observando lo que se toca (doc 01 §1.1). */
    static constexpr int keyboardLowest = 36;
    static constexpr int keyboardHighest = 84;

    juce::ComboBox audioDeviceBox, midiDeviceBox, bufferSizeBox, instrumentBox;
    juce::ToggleButton exclusiveToggle { "Modo exclusivo (menos latencia)" };
    juce::Slider reverbSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider volumeSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider tremoloSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label reverbLabel, volumeLabel, tremoloLabel, statusLabel, messageLabel;

    /** A qué destino aprende el botón. Un mando del teclado puede mover el
        volumen, el trémolo o la sala, y sólo el usuario sabe cuál quiere en
        cuál. */
    juce::ComboBox learnTargetBox;
    juce::TextButton panicButton { "Silencio" };
    juce::ToggleButton startupToggle { "Abrir con Windows" };
    juce::ToggleButton listenToggle { "Escuchar el PC" };
    juce::ComboBox listenDeviceBox;
    juce::TextButton learnButton { "Aprender" };
    juce::ComboBox kindBox, tonicBox, variantBox, optionBox, handBox, modeBox;
    juce::TextButton exerciseButton { "Empezar" };
    juce::TextButton importButton { "Abrir MIDI..." };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ToggleButton metronomeToggle { "Metronomo" };
    juce::Slider tempoSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    /** Ajustes recordados entre sesiones. Se llama `prefs` y no `settings`
        porque AudioDeviceHost::Settings ya ocupa ese nombre y confundirlos
        sería una tarde perdida. */
    Settings prefs;

    /** Modo desatendido: Keyla la arrancó Windows.

        La regla que lo gobierna todo es una sola: **Keyla no se queda con la
        tarjeta de sonido mientras no se la ve.** En modo exclusivo abrir el
        dispositivo deja mudo al resto del equipo, y un programa invisible que
        te quita el sonido del navegador sin explicar por qué es un programa
        que se acaba desinstalando.

        De ahí sale el resto: arrancada por Windows, Keyla espera minimizada y
        sin abrir nada. Enciendes el teclado y entonces —y sólo entonces— abre
        la tarjeta y se muestra. Lo apagas y suelta la tarjeta y se aparta.
        Encender el piano es la señal inequívoca de que quieres tocar. */
    const bool unattended;
    bool keyboardWasConnected { false };

    juce::String pendingMessage;
    bool messageIsError { false };
    // Lo que la ventana está enseñando ahora mismo. Sirve para no pelearse con
    // los mandos del teclado: si el valor no ha cambiado, no se toca el control.
    float shownVolume { -1.0f };
    float shownReverb { -1.0f };
    float shownTremolo { -1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace keyla::app
