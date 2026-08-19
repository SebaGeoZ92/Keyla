#include "ExerciseRunner.h"

#include "../music/Pitch.h"
#include "../text/Utf8.h"

#include <algorithm>
#include <map>

namespace keyla::core
{

using keyla::operator""_u8;

// ── Informe ─────────────────────────────────────────────────────────────────

std::vector<int> ExerciseReport::troubleSpots() const
{
    std::map<int, int> countByExpected;

    for (const auto& wrong : wrongNotes)
        ++countByExpected[wrong.expectedPitch];

    std::vector<std::pair<int, int>> sorted (countByExpected.begin(), countByExpected.end());

    std::sort (sorted.begin(), sorted.end(),
               [] (const auto& a, const auto& b) { return a.second > b.second; });

    std::vector<int> pitches;

    for (const auto& entry : sorted)
        pitches.push_back (entry.first);

    return pitches;
}

juce::String ExerciseReport::summary() const
{
    if (totalEvents == 0)
        return "Sin datos."_u8;

    if (eventsWithMistakes == 0)
        return "Entero a la primera, sin una nota de más."_u8;

    juce::String text;
    text << "Te trabaste en " << juce::String (eventsWithMistakes)
         << (eventsWithMistakes == 1 ? " nota de " : " notas de ")
         << juce::String (totalEvents) << ".";

    const auto spots = troubleSpots();

    if (! spots.empty())
    {
        // Se nombran como mucho tres: una lista de quince sitios problemáticos
        // no se puede usar para practicar mañana.
        juce::StringArray names;

        for (std::size_t i = 0; i < spots.size() && i < 3; ++i)
            names.add (noteName (spots[i]));

        text << " Donde más: " << names.joinIntoString (", ") << ".";
    }

    return text;
}

// ── Máquina de estados ──────────────────────────────────────────────────────

void ExerciseRunner::start (Exercise exerciseToRun, double startSeconds)
{
    current = std::move (exerciseToRun);
    outcomes.assign (current.events.size(), EventOutcome::pending);

    satisfiedInCurrentEvent.clear();
    heldPitches.clear();
    wrongNotes.clear();
    lastWrong = { -1, 0, 0, 0 };

    cursor = 0;
    mistakesOnCurrentEvent = 0;
    running = ! current.events.empty();
    finished = current.events.empty();
    startedAt = startSeconds;
    endedAt = startSeconds;
}

void ExerciseRunner::stop()
{
    running = false;
}

const ExpectedEvent* ExerciseRunner::currentEvent() const noexcept
{
    if (cursor < 0 || cursor >= static_cast<int> (current.events.size()))
        return nullptr;

    return &current.events[static_cast<std::size_t> (cursor)];
}

std::vector<int> ExerciseRunner::pendingPitches() const
{
    std::vector<int> pending;

    if (const auto* event = currentEvent())
        for (auto pitch : event->pitches)
            if (satisfiedInCurrentEvent.find (pitch) == satisfiedInCurrentEvent.end())
                pending.push_back (pitch);

    return pending;
}

bool ExerciseRunner::noteOn (int pitch, double seconds)
{
    if (! running || finished)
        return false;

    const auto* event = currentEvent();

    if (event == nullptr)
        return false;

    heldPitches.insert (pitch);

    if (event->contains (pitch))
    {
        satisfiedInCurrentEvent.insert (pitch);

        // El evento se cumple cuando están **todas** sus alturas. En un acorde
        // eso permite montarlo nota a nota, que es como se estudia.
        if (satisfiedInCurrentEvent.size() >= event->pitches.size())
        {
            advanceCursor (seconds);
            return true;
        }

        return false;
    }

    // Nota que no tocaba. En modo espera **no se avanza**: el ejercicio se
    // queda quieto hasta que aciertas, y ése es todo el feedback que hace falta
    // durante la ejecución (doc 01 §1.5). Nada de cruces rojas.
    ++mistakesOnCurrentEvent;

    if (outcomes[static_cast<std::size_t> (cursor)] == EventOutcome::pending)
        outcomes[static_cast<std::size_t> (cursor)] = EventOutcome::withMistakes;

    // Se apunta contra cuál de las alturas pendientes se falló: la más cercana,
    // que es la que el alumno estaba buscando.
    int nearest = event->pitches.empty() ? pitch : event->pitches.front();

    for (auto expected : event->pitches)
        if (std::abs (expected - pitch) < std::abs (nearest - pitch))
            nearest = expected;

    WrongNote wrong;
    wrong.eventIndex = cursor;
    wrong.playedPitch = pitch;
    wrong.expectedPitch = nearest;
    wrong.semitonesOff = pitch - nearest;

    wrongNotes.push_back (wrong);
    lastWrong = wrong;

    return false;
}

void ExerciseRunner::noteOff (int pitch)
{
    heldPitches.erase (pitch);
}

void ExerciseRunner::advanceCursor (double seconds)
{
    if (outcomes[static_cast<std::size_t> (cursor)] == EventOutcome::pending)
        outcomes[static_cast<std::size_t> (cursor)] = EventOutcome::clean;

    ++cursor;
    mistakesOnCurrentEvent = 0;
    satisfiedInCurrentEvent.clear();
    lastWrong = { -1, 0, 0, 0 };

    if (cursor >= static_cast<int> (current.events.size()))
    {
        finished = true;
        running = false;
        endedAt = seconds;
    }
}

ExerciseReport ExerciseRunner::report() const
{
    ExerciseReport result;

    result.totalEvents = static_cast<int> (current.events.size());
    result.wrongNotes = wrongNotes;
    result.secondsTaken = std::max (0.0, endedAt - startedAt);

    for (auto outcome : outcomes)
    {
        if (outcome == EventOutcome::clean)
            ++result.cleanEvents;
        else if (outcome == EventOutcome::withMistakes)
            ++result.eventsWithMistakes;
    }

    return result;
}

} // namespace keyla::core
