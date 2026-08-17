#pragma once

#include "../EngineSnapshot.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace keyla::app
{

/** El teclado en pantalla. Lo primero de la lista de prioridades del doc 02 §7:
    barato, imprescindible, y es lo que conecta la pantalla con las manos.

    Pinta tres estados distintos, no dos:

    - **pulsada** — la tecla está bajada ahora mismo;
    - **sostenida por el pedal** — la tecla está suelta pero la nota suena;
    - apagada.

    Esa distinción del medio es la que casi nadie hace y la que evita que el
    teclado mienta cuando usas el pedal (doc 02 §4).

    No recibe notificaciones del audio: lee un snapshot cuando le dicen
    (invariante 5).
*/
class PianoKeyboardView final : public juce::Component
{
public:
    PianoKeyboardView();

    /** Rango visible, en notas MIDI. El SE49 son 49 teclas desde Do1. */
    void setRange (int lowestNote, int highestNote);

    /** Lo llama el timer de la UI con el último snapshot. Sólo repinta si algo
        cambió: repintar 60 veces por segundo un teclado quieto es tirar CPU. */
    void updateFrom (const EngineSnapshot& snapshot);

    void paint (juce::Graphics& g) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;

    /** Notas tocadas con el ratón, para poder probar sin teclado enchufado. */
    std::function<void (int note, int velocity)> onNoteOn;
    std::function<void (int note)> onNoteOff;

private:
    struct KeyLayout
    {
        int note { 0 };
        juce::Rectangle<float> bounds;
        bool isBlack { false };
    };

    void rebuildLayout();
    int noteAt (juce::Point<float> position) const;
    static bool isBlackKey (int note) noexcept;

    int lowest { 36 };
    int highest { 84 };

    std::vector<KeyLayout> whiteKeys, blackKeys;

    std::uint64_t keysDown[2] { 0, 0 };
    std::uint64_t sounding[2] { 0, 0 };
    int mouseNote { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoKeyboardView)
};

} // namespace keyla::app
