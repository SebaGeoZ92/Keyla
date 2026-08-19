#include "ExerciseView.h"

#include <core/music/Pitch.h>
#include <core/text/Utf8.h>

namespace keyla::app
{

using keyla::operator""_u8;

namespace
{
    /** Cuántos fallos seguidos en el mismo sitio hacen falta antes de soplar la
        respuesta. Dos es el número: uno puede ser un resbalón y no merece
        interrupción; a partir del segundo el alumno está buscando y una pista
        ayuda más que dejarlo dar palos de ciego. */
    constexpr int mistakesBeforeHelping = 2;

    juce::String fingerText (const std::vector<std::uint8_t>& fingers)
    {
        if (fingers.empty())
            return {};

        juce::StringArray numbers;

        for (auto finger : fingers)
            numbers.add (juce::String (static_cast<int> (finger)));

        return "   dedo " + numbers.joinIntoString ("-");
    }
}

ExerciseView::ExerciseView()
{
    setOpaque (false);
    setInterceptsMouseClicks (false, false);
}

void ExerciseView::showIdle()
{
    idle = true;
    finished = false;
    title.clear();
    hint.clear();
    positionText.clear();
    nextText.clear();
    nudge.clear();
    repaint();
}

void ExerciseView::showRangeMessage (const juce::String& message)
{
    rangeMessage = message;
    repaint();
}

void ExerciseView::refresh (const core::ExerciseRunner& runner)
{
    const auto& exercise = runner.exercise();

    if (exercise.isEmpty())
    {
        showIdle();
        return;
    }

    idle = false;
    title = exercise.name;
    hint = exercise.hint;

    const int total = runner.totalEvents();
    const int index = runner.currentEventIndex();

    progress = total > 0 ? juce::jlimit (0.0, 1.0, static_cast<double> (index) / total) : 0.0;
    finished = runner.isFinished();

    if (finished)
    {
        const auto report = runner.report();

        positionText = "Terminado"_u8;
        nextText = report.summary();
        nudge.clear();
        repaint();
        return;
    }

    positionText = juce::String (index + 1) + " de " + juce::String (total);

    if (const auto* event = runner.currentEvent())
    {
        juce::StringArray names;

        for (auto pitch : event->pitches)
            names.add (core::noteName (pitch));

        nextText = names.joinIntoString (" + ") + fingerText (event->fingers);
    }

    // La pista sólo aparece cuando de verdad se está atascado, y dice el
    // intervalo en vez de regañar: "un semitono abajo" es información, "mal"
    // no lo es (doc 02 §5).
    nudge.clear();

    if (runner.consecutiveMistakesHere() >= mistakesBeforeHelping && runner.hasLastWrongNote())
    {
        const auto wrong = runner.lastWrongNote();
        const int distance = std::abs (wrong.semitonesOff);

        nudge = "Tocaste "_u8 + core::noteName (wrong.playedPitch) + ": "
              + juce::String (distance)
              + (distance == 1 ? " semitono " : " semitonos ")
              + (wrong.semitonesOff > 0 ? "por encima."_u8 : "por debajo."_u8);
    }

    repaint();
}

void ExerciseView::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().reduced (4, 0);

    if (idle)
    {
        g.setColour (juce::Colour { 0xff4a4f57 });
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (rangeMessage.isNotEmpty() ? rangeMessage
                                              : "Elige un ejercicio y pulsa Empezar."_u8,
                    area, juce::Justification::centredLeft, false);
        return;
    }

    // ── Barra de progreso ───────────────────────────────────────────────────
    auto barArea = area.removeFromBottom (4).toFloat();

    g.setColour (juce::Colour { 0xff2b2f36 });
    g.fillRect (barArea);

    g.setColour (finished ? juce::Colour { 0xff7fb069 } : juce::Colour { 0xff4f9dd9 });
    g.fillRect (barArea.withWidth (barArea.getWidth() * static_cast<float> (progress)));

    area.removeFromBottom (6);

    // ── Título y posición ───────────────────────────────────────────────────
    auto titleRow = area.removeFromTop (20);

    g.setColour (juce::Colour { 0xffb9c0c9 });
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (title, titleRow.removeFromLeft (titleRow.getWidth() - 110),
                juce::Justification::centredLeft, true);

    g.setColour (juce::Colour { 0xff8f98a3 });
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (positionText, titleRow, juce::Justification::centredRight, false);

    // ── Lo que toca ahora, o el resumen al terminar ─────────────────────────
    auto mainRow = area.removeFromTop (26);

    g.setColour (finished ? juce::Colour { 0xff9fd07f } : juce::Colour { 0xffe8eaed });
    g.setFont (juce::FontOptions (finished ? 15.0f : 20.0f, juce::Font::bold));
    g.drawText (nextText, mainRow, juce::Justification::centredLeft, true);

    // ── Pista o consejo ─────────────────────────────────────────────────────
    if (nudge.isNotEmpty())
    {
        g.setColour (juce::Colour { 0xffe0b062 });
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (nudge, area, juce::Justification::centredLeft, true);
    }
    else if (! finished && hint.isNotEmpty())
    {
        g.setColour (juce::Colour { 0xff6c737d });
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (hint, area, juce::Justification::centredLeft, true);
    }
}

} // namespace keyla::app
