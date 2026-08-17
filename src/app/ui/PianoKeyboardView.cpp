#include "PianoKeyboardView.h"

namespace keyla::app
{

namespace
{
    const juce::Colour whiteKeyColour     { 0xfff7f7f4 };
    const juce::Colour blackKeyColour     { 0xff23252a };
    const juce::Colour pressedColour      { 0xff4f9dd9 };
    const juce::Colour pedalColour        { 0xff2c5c7d };
    const juce::Colour outlineColour      { 0xff54585f };
}

PianoKeyboardView::PianoKeyboardView()
{
    setOpaque (true);
}

bool PianoKeyboardView::isBlackKey (int note) noexcept
{
    switch (((note % 12) + 12) % 12)
    {
        case 1: case 3: case 6: case 8: case 10:  return true;
        default:                                  return false;
    }
}

void PianoKeyboardView::setRange (int lowestNote, int highestNote)
{
    lowest = juce::jlimit (0, 127, lowestNote);
    highest = juce::jlimit (lowest + 11, 127, highestNote);

    // Empezar en una tecla negra dejaría media tecla cortada en el borde.
    while (lowest > 0 && isBlackKey (lowest))
        --lowest;

    while (highest < 127 && isBlackKey (highest))
        ++highest;

    rebuildLayout();
    repaint();
}

void PianoKeyboardView::resized()
{
    rebuildLayout();
}

void PianoKeyboardView::rebuildLayout()
{
    whiteKeys.clear();
    blackKeys.clear();

    int numWhite = 0;

    for (int note = lowest; note <= highest; ++note)
        if (! isBlackKey (note))
            ++numWhite;

    if (numWhite == 0)
        return;

    const auto area = getLocalBounds().toFloat();
    const float whiteWidth = area.getWidth() / static_cast<float> (numWhite);
    const float blackWidth = whiteWidth * 0.62f;
    const float blackHeight = area.getHeight() * 0.62f;

    float x = area.getX();

    for (int note = lowest; note <= highest; ++note)
    {
        if (isBlackKey (note))
            continue;

        whiteKeys.push_back ({ note, { x, area.getY(), whiteWidth, area.getHeight() }, false });
        x += whiteWidth;
    }

    // Las negras se colocan después, sobre la juntura de las dos blancas que
    // las rodean, para que queden por encima al pintar y al pulsar.
    for (int note = lowest; note <= highest; ++note)
    {
        if (! isBlackKey (note))
            continue;

        // La blanca inmediatamente anterior existe siempre: el rango empieza
        // en blanca por construcción.
        float leftEdge = area.getX();

        for (const auto& white : whiteKeys)
            if (white.note < note)
                leftEdge = white.bounds.getRight();

        blackKeys.push_back ({ note,
                               { leftEdge - blackWidth * 0.5f, area.getY(), blackWidth, blackHeight },
                               true });
    }
}

void PianoKeyboardView::updateFrom (const EngineSnapshot& snapshot)
{
    if (keysDown[0] == snapshot.keysDown[0] && keysDown[1] == snapshot.keysDown[1]
        && sounding[0] == snapshot.sounding[0] && sounding[1] == snapshot.sounding[1])
        return;

    keysDown[0] = snapshot.keysDown[0];
    keysDown[1] = snapshot.keysDown[1];
    sounding[0] = snapshot.sounding[0];
    sounding[1] = snapshot.sounding[1];

    repaint();
}

void PianoKeyboardView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour { 0xff17181c });

    for (const auto& key : whiteKeys)
    {
        const bool down = EngineSnapshot::hasBit (keysDown, key.note);
        const bool held = ! down && EngineSnapshot::hasBit (sounding, key.note);

        g.setColour (down ? pressedColour
                          : held ? pedalColour.brighter (0.7f)
                                 : whiteKeyColour);
        g.fillRect (key.bounds.reduced (0.5f));

        g.setColour (outlineColour);
        g.drawRect (key.bounds, 0.5f);

        // Referencia visual: el Do de cada octava lleva su nombre.
        if (key.note % 12 == 0)
        {
            g.setColour (down ? juce::Colours::white : outlineColour);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText ("C" + juce::String (key.note / 12 - 1),
                        key.bounds.withTrimmedBottom (4.0f),
                        juce::Justification::centredBottom, false);
        }
    }

    for (const auto& key : blackKeys)
    {
        const bool down = EngineSnapshot::hasBit (keysDown, key.note);
        const bool held = ! down && EngineSnapshot::hasBit (sounding, key.note);

        g.setColour (down ? pressedColour.darker (0.2f)
                          : held ? pedalColour
                                 : blackKeyColour);
        g.fillRoundedRectangle (key.bounds, 2.0f);

        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (key.bounds, 2.0f, 1.0f);
    }
}

// ── Ratón ───────────────────────────────────────────────────────────────────

int PianoKeyboardView::noteAt (juce::Point<float> position) const
{
    // Las negras primero: están por encima.
    for (const auto& key : blackKeys)
        if (key.bounds.contains (position))
            return key.note;

    for (const auto& key : whiteKeys)
        if (key.bounds.contains (position))
            return key.note;

    return -1;
}

void PianoKeyboardView::mouseDown (const juce::MouseEvent& event)
{
    const auto note = noteAt (event.position);

    if (note < 0)
        return;

    mouseNote = note;

    if (onNoteOn != nullptr)
    {
        // La velocity sale de la altura del clic dentro de la tecla: abajo
        // fuerte, arriba flojo. No es un teclado sensible, pero permite probar
        // la dinámica sin enchufar nada.
        const auto height = juce::jmax (1.0f, static_cast<float> (getHeight()));
        const auto ratio = juce::jlimit (0.0f, 1.0f, event.position.y / height);
        onNoteOn (note, juce::jlimit (20, 127, static_cast<int> (25.0f + ratio * 102.0f)));
    }
}

void PianoKeyboardView::mouseDrag (const juce::MouseEvent& event)
{
    const auto note = noteAt (event.position);

    if (note == mouseNote)
        return;

    // Arrastrar de una tecla a otra ligado, como en un piano de verdad.
    if (mouseNote >= 0 && onNoteOff != nullptr)
        onNoteOff (mouseNote);

    mouseNote = note;

    if (note >= 0 && onNoteOn != nullptr)
        onNoteOn (note, 80);
}

void PianoKeyboardView::mouseUp (const juce::MouseEvent&)
{
    if (mouseNote >= 0 && onNoteOff != nullptr)
        onNoteOff (mouseNote);

    mouseNote = -1;
}

} // namespace keyla::app
