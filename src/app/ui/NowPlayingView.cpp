#include "NowPlayingView.h"
#include "Theme.h"

#include <core/music/ChordRecognizer.h>
#include <core/music/Pitch.h>
#include <core/text/Utf8.h>

namespace keyla::app
{

using keyla::operator""_u8;

NowPlayingView::NowPlayingView()
{
    setOpaque (false);
    setInterceptsMouseClicks (false, false);
}

void NowPlayingView::updateFrom (const EngineSnapshot& snapshot)
{
    if (snapshot.sounding[0] == lastSounding[0] && snapshot.sounding[1] == lastSounding[1])
        return;

    lastSounding[0] = snapshot.sounding[0];
    lastSounding[1] = snapshot.sounding[1];

    std::vector<int> notes;
    notes.reserve (16);

    for (int note = 0; note < 128; ++note)
        if (EngineSnapshot::hasBit (snapshot.sounding, note))
            notes.push_back (note);

    rebuildText (notes);
    repaint();
}

void NowPlayingView::rebuildText (const std::vector<int>& notes)
{
    if (notes.empty())
    {
        // No se borra: se atenúa. Lo último que tocaste sigue leyéndose hasta
        // que toques otra cosa.
        holdingLast = hasContent;
        return;
    }

    headline.clear();
    detail.clear();
    hasContent = true;
    holdingLast = false;

    // ── Una nota ────────────────────────────────────────────────────────────
    if (notes.size() == 1)
    {
        headline = core::noteName (notes.front());
        detail = "nota suelta";
        return;
    }

    // ── Dos notas: un intervalo, que es lo único que se puede afirmar ───────
    if (notes.size() == 2)
    {
        headline = core::noteName (notes[0]) + "  ·  "_u8 + core::noteName (notes[1]);
        detail = core::intervalName (notes[0], notes[1])
               + "   ("_u8 + core::intervalShortName (notes[1] - notes[0]) + ")";
        return;
    }

    // ── Tres o más: se intenta cifrar ───────────────────────────────────────
    const auto chord = core::ChordRecognizer::recognise (notes);

    if (chord.recognised)
    {
        headline = chord.symbol;
        detail = chord.description;
        return;
    }

    // No reconocido. Se enseñan las notas y se dice que no se sabe, en vez de
    // inventarse un cifrado: un análisis falso es peor que ninguno.
    juce::StringArray names;

    for (auto note : notes)
        names.add (core::noteName (note));

    headline = names.joinIntoString ("  ");
    detail = juce::String (notes.size()) + " notas, sin cifrado reconocible"_u8;
}

void NowPlayingView::paint (juce::Graphics& g)
{
    auto area = theme::paintCard (g, getLocalBounds(), "TÚ TOCAS"_u8);

    if (! hasContent)
    {
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (15.0f));
        g.drawFittedText ("Toca algo en el teclado."_u8, area.removeFromTop (40),
                          juce::Justification::topLeft, 2, 1.0f);
        return;
    }

    // Atenuado si es la lectura de algo que ya soltaste, para que se vea de un
    // vistazo si suena ahora o es lo anterior.
    const float alpha = holdingLast ? 0.45f : 1.0f;

    // El cifrado en grande; si son notas sueltas sin cifrado, más pequeño para
    // que quepan, pero en el mismo sitio.
    auto headlineArea = area.removeFromTop (juce::jmin (64, area.getHeight() / 2));
    const bool isChordSymbol = headline.length() <= 8;

    g.setColour (theme::text.withAlpha (alpha));
    g.setFont (juce::FontOptions (isChordSymbol ? 50.0f : 26.0f, juce::Font::bold));
    g.drawFittedText (headline, headlineArea, juce::Justification::centredLeft, 1, 0.5f);

    g.setColour (theme::textSecondary.withAlpha (alpha));
    g.setFont (juce::FontOptions (15.0f));
    g.drawFittedText (detail, area.removeFromTop (44), juce::Justification::topLeft, 2, 0.9f);
}

} // namespace keyla::app
