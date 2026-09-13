#include "ListeningView.h"
#include "Theme.h"

#include <core/text/Utf8.h>

namespace keyla::app
{

using keyla::operator""_u8;

void ListeningView::paint (juce::Graphics& g)
{
    auto area = theme::paintCard (g, getLocalBounds(), "SUENA EN EL PC");

    const auto dimLine = [&g, &area] (const juce::String& message, juce::Colour colour)
    {
        g.setColour (colour);
        g.setFont (juce::FontOptions (15.0f));
        g.drawFittedText (message, area.removeFromTop (60), juce::Justification::topLeft, 3, 1.0f);
    };

    if (! current.active)
    {
        dimLine ("Activa Escuchar y pon una canción: Keyla va sacando los acordes y "
                 "te enciende las teclas."_u8, theme::textDim);
        return;
    }

    // El diagnóstico va antes que el cifrado: si no llega audio, decir "no
    // reconozco nada" sería echarle la culpa al reconocedor.
    if (! current.receivingAudio)
    {
        dimLine ("No llega nada de "_u8 + current.deviceName
                 + ". ¿Está sonando algo? Si sí, elige otra salida en Ajustes: hay "
                   "mezcladores virtuales que no dejan escucharse."_u8, theme::warning);
        return;
    }

    if (! current.hearingMusic)
    {
        dimLine ("Silencio en "_u8 + current.deviceName + "."_u8, theme::textDim);
        return;
    }

    // ── El acorde ───────────────────────────────────────────────────────────
    auto headline = area.removeFromTop (juce::jmin (64, area.getHeight() / 2));

    if (current.chordSymbol.isNotEmpty())
    {
        g.setColour (theme::accent);
        g.setFont (juce::FontOptions (50.0f, juce::Font::bold));
        g.drawFittedText (current.chordSymbol, headline, juce::Justification::centredLeft, 1, 0.6f);
    }
    else
    {
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (40.0f, juce::Font::bold));
        g.drawText ("¿?"_u8, headline, juce::Justification::centredLeft, false);
    }

    // ── Qué tocar: lo único de aquí que se usa con las manos ────────────────
    if (suggestion.isNotEmpty() && area.getHeight() >= 22)
    {
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (16.0f));
        g.drawFittedText ("Toca  "_u8 + suggestion, area.removeFromTop (24),
                          juce::Justification::centredLeft, 1, 0.8f);
    }

    // ── El contexto ─────────────────────────────────────────────────────────
    if (current.keyName.isNotEmpty() && area.getHeight() >= 20)
    {
        g.setColour (theme::textSecondary);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Tonalidad "_u8 + current.keyName, area.removeFromTop (22),
                    juce::Justification::centredLeft, true);
    }

    if (current.progression.isNotEmpty() && area.getHeight() >= 20)
    {
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (current.progression, area.removeFromTop (22),
                    juce::Justification::centredLeft, true);
    }
}

} // namespace keyla::app
