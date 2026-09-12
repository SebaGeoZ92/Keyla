#include "ListeningView.h"

#include <core/text/Utf8.h>

namespace keyla::app
{

using keyla::operator""_u8;

void ListeningView::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().reduced (4, 0);

    const juce::Colour dim { 0xff4a4f57 };
    const juce::Colour label { 0xff8f98a3 };

    if (! current.active)
    {
        g.setColour (dim);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText ("escucha apagada"_u8, area, juce::Justification::centredLeft, false);
        return;
    }

    // El diagnóstico va antes que el cifrado: si no llega audio, decir "no
    // reconozco nada" sería echarle la culpa al reconocedor.
    if (! current.receivingAudio)
    {
        g.setColour (juce::Colour { 0xffe4785e });
        g.setFont (juce::FontOptions (15.0f));
        g.drawText ("escuchando "_u8 + current.deviceName
                        + ": no llega nada. Pon musica, o elige otra salida — "_u8
                        + "hay mezcladores virtuales que no dejan escucharse."_u8,
                    area, juce::Justification::centredLeft, false);
        return;
    }

    if (! current.hearingMusic)
    {
        g.setColour (dim);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText ("escuchando "_u8 + current.deviceName + ": silencio"_u8,
                    area, juce::Justification::centredLeft, false);
        return;
    }

    auto headlineArea = area.removeFromLeft (juce::jmin (area.getWidth(), 200));

    if (current.chordSymbol.isNotEmpty())
    {
        g.setColour (juce::Colour { 0xff7fb069 });
        g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
        g.drawText (current.chordSymbol, headlineArea, juce::Justification::centredLeft, false);
    }
    else
    {
        g.setColour (dim);
        g.setFont (juce::FontOptions (17.0f));
        g.drawText ("¿?"_u8, headlineArea, juce::Justification::centredLeft, false);
    }

    juce::String detail;

    if (current.keyName.isNotEmpty())
        detail << "tonalidad " << current.keyName << "   ";

    if (current.progression.isNotEmpty())
        detail << current.progression;

    g.setColour (label);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (detail, area, juce::Justification::centredLeft, false);
}

} // namespace keyla::app
