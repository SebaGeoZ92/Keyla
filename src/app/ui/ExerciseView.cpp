#include "ExerciseView.h"
#include "Theme.h"

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
    showingReport = false;
    reportLines.clear();
    title.clear();
    hint.clear();
    positionText.clear();
    nextText.clear();
    nudge.clear();
    repaint();
}

void ExerciseView::showReport (const juce::String& exerciseName, const juce::StringArray& lines)
{
    idle = false;
    finished = true;
    showingReport = true;
    title = exerciseName;
    reportLines = lines;
    progress = 1.0;
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
    showingReport = false;
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
    auto area = theme::paintCard (g, getLocalBounds(), "EJERCICIO");

    if (idle)
    {
        g.setColour (rangeMessage.isNotEmpty() ? theme::warning : theme::textDim);
        g.setFont (juce::FontOptions (15.0f));
        g.drawFittedText (rangeMessage.isNotEmpty()
                              ? rangeMessage
                              : "Elige un ejercicio en la barra de arriba y pulsa Empezar."_u8,
                          area.removeFromTop (60), juce::Justification::topLeft, 3, 1.0f);
        return;
    }

    // ── Título y posición, con la barra de progreso justo debajo ────────────
    //
    // La barra iba al pie de la tarjeta, lejos de todo lo que describe. Pegada
    // al título se lee como lo que es: cuánto llevas de *este* ejercicio.
    auto titleRow = area.removeFromTop (22);

    g.setColour (theme::textSecondary);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (positionText, titleRow.removeFromRight (90), juce::Justification::centredRight, false);
    g.drawFittedText (title, titleRow, juce::Justification::centredLeft, 1, 0.8f);

    area.removeFromTop (4);
    auto barArea = area.removeFromTop (5).toFloat();

    g.setColour (theme::border);
    g.fillRoundedRectangle (barArea, 2.5f);

    g.setColour (finished ? theme::accent : theme::progress);
    g.fillRoundedRectangle (barArea.withWidth (barArea.getWidth() * static_cast<float> (progress)), 2.5f);

    // ── El informe, si lo hay ───────────────────────────────────────────────
    if (showingReport)
    {
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (14.0f));

        for (const auto& line : reportLines)
        {
            if (area.getHeight() < 18)
                break;

            g.drawFittedText (line, area.removeFromTop (18), juce::Justification::centredLeft, 1, 0.85f);
        }

        return;
    }

    // ── Lo que toca ahora, en grande; o el resumen al terminar ──────────────
    area.removeFromTop (4);
    auto mainRow = area.removeFromTop (finished ? 30 : 48);

    g.setColour (finished ? theme::accent : theme::text);
    g.setFont (juce::FontOptions (finished ? 18.0f : 36.0f, juce::Font::bold));
    g.drawFittedText (nextText, mainRow, juce::Justification::centredLeft, 1, 0.6f);

    // ── Pista o consejo ─────────────────────────────────────────────────────
    area.removeFromTop (4);

    if (nudge.isNotEmpty())
    {
        g.setColour (theme::warning);
        g.setFont (juce::FontOptions (15.0f));
        g.drawFittedText (nudge, area, juce::Justification::topLeft, 2, 0.9f);
    }
    else if (! finished && hint.isNotEmpty())
    {
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawFittedText (hint, area, juce::Justification::topLeft, 2, 0.9f);
    }
}

} // namespace keyla::app
