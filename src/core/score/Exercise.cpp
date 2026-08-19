#include "Exercise.h"

#include "../text/Utf8.h"

#include <algorithm>

namespace keyla::core
{

using keyla::operator""_u8;

bool ExpectedEvent::contains (int pitch) const noexcept
{
    return std::find (pitches.begin(), pitches.end(),
                      static_cast<std::uint8_t> (pitch)) != pitches.end();
}

int Exercise::lowestPitch() const noexcept
{
    int lowest = 127;

    for (const auto& event : events)
        for (auto pitch : event.pitches)
            lowest = std::min (lowest, static_cast<int> (pitch));

    return events.empty() ? -1 : lowest;
}

int Exercise::highestPitch() const noexcept
{
    int highest = 0;

    for (const auto& event : events)
        for (auto pitch : event.pitches)
            highest = std::max (highest, static_cast<int> (pitch));

    return events.empty() ? -1 : highest;
}

int Exercise::totalNotes() const noexcept
{
    int total = 0;

    for (const auto& event : events)
        total += static_cast<int> (event.pitches.size());

    return total;
}

void Exercise::transposeOctaves (int octaves)
{
    if (octaves == 0)
        return;

    const int semitones = octaves * 12;

    for (auto& event : events)
        for (auto& pitch : event.pitches)
            pitch = static_cast<std::uint8_t> (std::clamp (pitch + semitones, 0, 127));
}

RangeFit fitToKeyboardRange (Exercise& exercise, int lowest, int highest)
{
    RangeFit fit;

    if (exercise.isEmpty())
        return fit;

    const int exerciseLow = exercise.lowestPitch();
    const int exerciseHigh = exercise.highestPitch();

    if (exerciseLow >= lowest && exerciseHigh <= highest)
        return fit;

    const int span = exerciseHigh - exerciseLow;

    if (span > highest - lowest)
    {
        fit.outcome = RangeFit::Outcome::doesNotFit;
        fit.explanation = "Este ejercicio abarca "_u8 + juce::String (span / 12 + 1)
                        + " octavas y tu teclado tiene "_u8
                        + juce::String ((highest - lowest) / 12 + 1)
                        + ". No cabe ni moviéndolo."_u8;
        return fit;
    }

    // Se mueve por octavas enteras, nunca por semitonos: transportar una escala
    // de Do a Do sostenido cambia la digitación y el ejercicio deja de ser el
    // que el alumno quería practicar.
    int octaves = 0;

    while (exerciseLow + octaves * 12 < lowest)
        ++octaves;

    while (exerciseHigh + octaves * 12 > highest)
        --octaves;

    exercise.transposeOctaves (octaves);

    fit.outcome = RangeFit::Outcome::transposed;
    fit.octavesMoved = octaves;
    fit.explanation = octaves > 0
                    ? "Subido "_u8 + juce::String (octaves) + " octava"_u8
                        + (octaves == 1 ? "" : "s") + " para que quepa en tu teclado."_u8
                    : "Bajado "_u8 + juce::String (-octaves) + " octava"_u8
                        + (octaves == -1 ? "" : "s") + " para que quepa en tu teclado."_u8;

    return fit;
}

} // namespace keyla::core
