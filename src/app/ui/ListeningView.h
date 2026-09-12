#pragma once

#include "../ListeningEngine.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace keyla::app
{

/** Lo que Keyla oye del ordenador: el acorde grande y, debajo, el contexto.

    Enseña **por qué** no hay cifrado cuando no lo hay. "No llega audio", "llega
    audio pero es silencio" y "suena algo y no lo reconozco" son tres estados
    distintos con tres soluciones distintas, y un cartel vacío los confunde a
    los tres.
*/
class ListeningView final : public juce::Component
{
public:
    ListeningView()
    {
        setOpaque (false);
        setInterceptsMouseClicks (false, false);
    }

    void updateFrom (const ListeningReading& reading)
    {
        if (reading.chordSymbol == current.chordSymbol
            && reading.keyName == current.keyName
            && reading.progression == current.progression
            && reading.active == current.active
            && reading.receivingAudio == current.receivingAudio
            && reading.hearingMusic == current.hearingMusic)
            return;

        current = reading;
        repaint();
    }

    /** Qué teclas tocar sobre lo que suena. La calcula la ventana, no el
        motor de escucha: es una decisión musical, no una medición. */
    void setSuggestion (const juce::String& text)
    {
        if (text == suggestion)
            return;

        suggestion = text;
        repaint();
    }

    void paint (juce::Graphics& g) override;

private:
    ListeningReading current;
    juce::String suggestion;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ListeningView)
};

} // namespace keyla::app
