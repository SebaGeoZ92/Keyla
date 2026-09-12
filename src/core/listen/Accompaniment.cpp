#include "Accompaniment.h"

#include "../music/Pitch.h"
#include "../score/ProgressionGenerator.h"
#include "../text/Utf8.h"

#include <algorithm>

namespace keyla::core
{

using keyla::operator""_u8;

std::vector<int> AccompanimentSuggestion::allNotes() const
{
    std::vector<int> notes = rightHand;

    if (bassNote >= 0)
        notes.push_back (bassNote);

    std::sort (notes.begin(), notes.end());
    notes.erase (std::unique (notes.begin(), notes.end()), notes.end());

    return notes;
}

void AccompanimentCoach::reset()
{
    previousHand.clear();
}

AccompanimentSuggestion AccompanimentCoach::suggest (int rootPitchClass, ChordQuality quality)
{
    AccompanimentSuggestion suggestion;

    if (rootPitchClass < 0 || quality == ChordQuality::unknown)
        return suggestion;

    suggestion.rightHand = voiceChordNear (rootPitchClass, quality, previousHand, opts.centrePitch);

    if (suggestion.rightHand.empty())
        return suggestion;

    previousHand = suggestion.rightHand;

    if (opts.withLeftHandBass)
        suggestion.bassNote = bassNoteFor (rootPitchClass, suggestion.rightHand);

    suggestion.valid = true;

    // El nombre de las notas, no el cifrado: el cifrado ya se ve arriba, y lo
    // que hace falta aquí es saber qué teclas tocar.
    juce::StringArray names;

    for (auto pitch : suggestion.rightHand)
        names.add (noteName (pitch));

    suggestion.description = names.joinIntoString (" ");

    if (suggestion.bassNote >= 0)
        suggestion.description += ", bajo "_u8 + noteName (suggestion.bassNote);

    return suggestion;
}

} // namespace keyla::core
