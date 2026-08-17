#include "NowPlayingView.h"

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
    headline.clear();
    detail.clear();
    hasContent = ! notes.empty();

    if (notes.empty())
        return;

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
    auto area = getLocalBounds().reduced (4, 0);

    if (! hasContent)
    {
        g.setColour (juce::Colour { 0xff4a4f57 });
        g.setFont (juce::FontOptions (15.0f));
        g.drawText ("toca algo"_u8, area, juce::Justification::centredLeft, false);
        return;
    }

    auto headlineArea = area.removeFromLeft (juce::jmin (area.getWidth(), 340));

    g.setColour (juce::Colour { 0xffe8eaed });
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText (headline, headlineArea, juce::Justification::centredLeft, false);

    g.setColour (juce::Colour { 0xff8f98a3 });
    g.setFont (juce::FontOptions (15.0f));
    g.drawText (detail, area, juce::Justification::centredLeft, false);
}

} // namespace keyla::app
