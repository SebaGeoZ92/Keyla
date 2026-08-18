#pragma once

#include "../EngineSnapshot.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace keyla::app
{

/** Qué estás tocando ahora mismo: la nota, el intervalo o el acorde.

    Feedback **no valorativo**: dice qué es, nunca si está bien. El doc 01 §1.5
    es explícito en que marcar aciertos y errores en tiempo real es mala
    pedagogía — rompe la concentración y enseña a parar y repetir compás a
    compás, que es el peor hábito de práctica que existe. El juicio va al
    informe del final; esto es sólo un espejo.

    Lee el snapshot cuando le dicen. No se le notifica desde el audio.
*/
class NowPlayingView final : public juce::Component
{
public:
    NowPlayingView();

    void updateFrom (const EngineSnapshot& snapshot);

    void paint (juce::Graphics& g) override;

private:
    void rebuildText (const std::vector<int>& notes);

    std::uint64_t lastSounding[2] { 0, 0 };
    bool hasContent { false };

    /** Cuando se sueltan todas las teclas, el texto **se queda**. Tocando no da
        tiempo a mirar la pantalla: para cuando levantas la vista ya has soltado
        y, si se borrara, nunca llegarías a leer lo que acabas de tocar. Se
        atenúa para que se distinga de lo que suena ahora mismo. */
    bool holdingLast { false };

    juce::String headline;      // "Do mayor"  ·  "C4"
    juce::String detail;        // cifrado, intervalo, notas sueltas

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NowPlayingView)
};

} // namespace keyla::app
