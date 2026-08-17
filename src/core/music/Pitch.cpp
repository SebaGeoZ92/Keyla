#include "Pitch.h"

#include "../text/Utf8.h"

#include <algorithm>

namespace keyla::core
{

using keyla::operator""_u8;

namespace
{
    const char* const sharpNames[12] = { "C", "C#", "D", "D#", "E", "F",
                                         "F#", "G", "G#", "A", "A#", "B" };

    const char* const flatNames[12]  = { "C", "Db", "D", "Eb", "E", "F",
                                         "Gb", "G", "Ab", "A", "Bb", "B" };
}

juce::String pitchClassName (int pitchClass, Accidental accidental)
{
    const int index = ((pitchClass % 12) + 12) % 12;

    return accidental == Accidental::flats ? flatNames[index] : sharpNames[index];
}

juce::String noteName (int midiNote, Accidental accidental)
{
    if (midiNote < 0 || midiNote > 127)
        return "—"_u8;

    return pitchClassName (pitchClassOf (midiNote), accidental)
         + juce::String (octaveOf (midiNote));
}

juce::String intervalShortName (int semitones)
{
    // Nombres de la forma simple. El tritono se deja como tal: llamarlo 4ª
    // aumentada o 5ª disminuida depende de una tonalidad que aquí no se conoce.
    static const char* const names[12] = {
        "1J", "2m", "2M", "3m", "3M", "4J", "TT", "5J", "6m", "6M", "7m", "7M"
    };

    return names[((semitones % 12) + 12) % 12];
}

juce::String intervalName (int lowNote, int highNote)
{
    if (lowNote > highNote)
        std::swap (lowNote, highNote);

    const int distance = highNote - lowNote;
    const int simple = distance % 12;
    const int octaves = distance / 12;

    static const char* const names[12] = {
        "unísono", "2ª menor", "2ª mayor", "3ª menor", "3ª mayor", "4ª justa",
        "tritono", "5ª justa", "6ª menor", "6ª mayor", "7ª menor", "7ª mayor"
    };

    juce::String result = juce::String (juce::CharPointer_UTF8 (names[simple]));

    if (octaves == 1 && simple == 0)
        return "octava"_u8;

    if (octaves > 0)
        result += " + " + juce::String (octaves)
                + (octaves == 1 ? " octava"_u8 : " octavas"_u8);

    return result;
}

} // namespace keyla::core
