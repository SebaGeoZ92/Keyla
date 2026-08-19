#include "ExerciseGenerator.h"

#include "../music/Pitch.h"
#include "../text/Utf8.h"

#include <algorithm>

namespace keyla::core
{

using keyla::operator""_u8;

std::vector<int> scaleIntervals (ScaleType type)
{
    switch (type)
    {
        case ScaleType::major:            return { 0, 2, 4, 5, 7, 9, 11 };
        case ScaleType::naturalMinor:     return { 0, 2, 3, 5, 7, 8, 10 };
        case ScaleType::harmonicMinor:    return { 0, 2, 3, 5, 7, 8, 11 };
        case ScaleType::melodicMinor:     return { 0, 2, 3, 5, 7, 9, 11 };
        case ScaleType::chromatic:        return { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
        case ScaleType::pentatonicMajor:  return { 0, 2, 4, 7, 9 };
        case ScaleType::pentatonicMinor:  return { 0, 3, 5, 7, 10 };
        case ScaleType::blues:            return { 0, 3, 5, 6, 7, 10 };
    }

    return { 0, 2, 4, 5, 7, 9, 11 };
}

std::vector<int> arpeggioIntervals (ArpeggioType type)
{
    switch (type)
    {
        case ArpeggioType::major:        return { 0, 4, 7 };
        case ArpeggioType::minor:        return { 0, 3, 7 };
        case ArpeggioType::dominant7:    return { 0, 4, 7, 10 };
        case ArpeggioType::major7:       return { 0, 4, 7, 11 };
        case ArpeggioType::minor7:       return { 0, 3, 7, 10 };
        case ArpeggioType::diminished:   return { 0, 3, 6 };
    }

    return { 0, 4, 7 };
}

juce::String scaleTypeName (ScaleType type)
{
    switch (type)
    {
        case ScaleType::major:            return "mayor";
        case ScaleType::naturalMinor:     return "menor natural";
        case ScaleType::harmonicMinor:    return "menor armónica"_u8;
        case ScaleType::melodicMinor:     return "menor melódica"_u8;
        case ScaleType::chromatic:        return "cromática"_u8;
        case ScaleType::pentatonicMajor:  return "pentatónica mayor"_u8;
        case ScaleType::pentatonicMinor:  return "pentatónica menor"_u8;
        case ScaleType::blues:            return "de blues";
    }

    return "";
}

juce::String arpeggioTypeName (ArpeggioType type)
{
    switch (type)
    {
        case ArpeggioType::major:       return "mayor";
        case ArpeggioType::minor:       return "menor";
        case ArpeggioType::dominant7:   return "séptima de dominante"_u8;
        case ArpeggioType::major7:      return "séptima mayor"_u8;
        case ArpeggioType::minor7:      return "menor séptima"_u8;
        case ArpeggioType::diminished:  return "disminuido";
    }

    return "";
}

namespace
{
    /** Digitación de escala de siete notas, la de toda la vida.

        Derecha subiendo: 1-2-3, pulgar por debajo, 1-2-3-4 y el 5 al cerrar.
        Izquierda subiendo: 5-4-3-2-1, cruce, 3-2 y el 1 al cerrar.

        Sólo para escalas de siete notas. En la cromática, las pentatónicas o el
        blues la digitación real depende de la tonalidad y del contexto, e
        inventarla sería enseñar a tocar mal: mejor no dar ninguna.
    */
    std::vector<std::uint8_t> heptatonicFingering (int noteIndex, int totalAscending, Hand hand)
    {
        static const int rightPattern[7] = { 1, 2, 3, 1, 2, 3, 4 };
        static const int leftPattern[7]  = { 5, 4, 3, 2, 1, 3, 2 };

        if (noteIndex == totalAscending - 1)
            return { static_cast<std::uint8_t> (hand == Hand::left ? 1 : 5) };

        // Al bajar se repite el patrón en espejo, que es lo que hace la mano.
        const int position = noteIndex < totalAscending
                           ? noteIndex % 7
                           : (2 * (totalAscending - 1) - noteIndex) % 7;

        const int safe = ((position % 7) + 7) % 7;

        return { static_cast<std::uint8_t> (hand == Hand::left ? leftPattern[safe]
                                                               : rightPattern[safe]) };
    }

    /** Alturas subiendo y, si se pide, bajando. La nota de arriba no se repite:
        al bajar se arranca desde la siguiente. */
    std::vector<int> buildLine (int root, const std::vector<int>& intervals, int octaves, bool descend)
    {
        std::vector<int> ascending;

        for (int octave = 0; octave < octaves; ++octave)
            for (auto interval : intervals)
                ascending.push_back (root + octave * 12 + interval);

        ascending.push_back (root + octaves * 12);      // la tónica de arriba cierra

        auto line = ascending;

        if (descend)
            for (int i = static_cast<int> (ascending.size()) - 2; i >= 0; --i)
                line.push_back (ascending[static_cast<std::size_t> (i)]);

        return line;
    }

    Exercise buildFromLine (const std::vector<int>& line, Hand hand, bool heptatonic, int ascendingCount)
    {
        Exercise exercise;
        exercise.events.reserve (line.size());

        for (std::size_t i = 0; i < line.size(); ++i)
        {
            ExpectedEvent event;
            event.pitches = { static_cast<std::uint8_t> (std::clamp (line[i], 0, 127)) };
            event.onsetBeat = static_cast<double> (i);
            event.durationBeats = 1.0;
            event.hand = hand;

            if (heptatonic)
                event.fingers = heptatonicFingering (static_cast<int> (i), ascendingCount, hand);

            exercise.events.push_back (std::move (event));
        }

        return exercise;
    }

    juce::String handName (Hand hand)
    {
        return hand == Hand::left ? "mano izquierda"_u8 : "mano derecha"_u8;
    }

    juce::String octaveCount (int octaves)
    {
        return juce::String (octaves) + (octaves == 1 ? " octava"_u8 : " octavas"_u8);
    }
}

Exercise generateScale (const ScaleRequest& request)
{
    const auto intervals = scaleIntervals (request.type);
    const int octaves = std::clamp (request.octaves, 1, 4);
    const auto line = buildLine (request.rootPitch, intervals, octaves, request.descend);
    const int ascendingCount = static_cast<int> (intervals.size()) * octaves + 1;

    auto exercise = buildFromLine (line, request.hand, intervals.size() == 7, ascendingCount);

    const auto rootName = pitchClassName (pitchClassOf (request.rootPitch));

    exercise.id = "scale." + rootName.toLowerCase() + "."
                + juce::String (static_cast<int> (request.type)) + "."
                + juce::String (octaves) + "oct."
                + (request.hand == Hand::left ? "lh" : "rh");

    exercise.name = "Escala de "_u8 + rootName + " " + scaleTypeName (request.type)
                  + "  ·  "_u8 + octaveCount (octaves)
                  + "  ·  "_u8 + handName (request.hand);

    exercise.hint = intervals.size() == 7
                  ? "Fíjate en el paso del pulgar: es donde casi todo el mundo pierde la regularidad."_u8
                  : "Busca que todas las notas suenen igual de fuertes y de separadas."_u8;

    return exercise;
}

Exercise generateArpeggio (const ArpeggioRequest& request)
{
    const auto intervals = arpeggioIntervals (request.type);
    const int octaves = std::clamp (request.octaves, 1, 4);
    const auto line = buildLine (request.rootPitch, intervals, octaves, request.descend);

    auto exercise = buildFromLine (line, request.hand, false, 0);

    const auto rootName = pitchClassName (pitchClassOf (request.rootPitch));

    exercise.id = "arpeggio." + rootName.toLowerCase() + "."
                + juce::String (static_cast<int> (request.type)) + "."
                + juce::String (octaves) + "oct."
                + (request.hand == Hand::left ? "lh" : "rh");

    exercise.name = "Arpegio de "_u8 + rootName + " " + arpeggioTypeName (request.type)
                  + "  ·  "_u8 + octaveCount (octaves)
                  + "  ·  "_u8 + handName (request.hand);

    exercise.hint = "Los saltos son grandes: llega a cada tecla antes de tocarla."_u8;

    return exercise;
}

std::vector<Exercise> defaultExercises()
{
    std::vector<Exercise> list;

    // Do mayor primero porque no tiene teclas negras: ahí se aprende el
    // movimiento antes de añadirle dificultad de lectura.
    list.push_back (generateScale ({ 60, ScaleType::major, 1, Hand::right, true }));
    list.push_back (generateScale ({ 60, ScaleType::major, 1, Hand::left, true }));
    list.push_back (generateScale ({ 60, ScaleType::major, 2, Hand::right, true }));
    list.push_back (generateScale ({ 57, ScaleType::naturalMinor, 1, Hand::right, true }));
    list.push_back (generateScale ({ 67, ScaleType::major, 1, Hand::right, true }));
    list.push_back (generateArpeggio ({ 60, ArpeggioType::major, 1, Hand::right, true }));
    list.push_back (generateArpeggio ({ 57, ArpeggioType::minor, 1, Hand::right, true }));
    list.push_back (generateScale ({ 60, ScaleType::pentatonicMinor, 1, Hand::right, true }));
    list.push_back (generateScale ({ 60, ScaleType::chromatic, 1, Hand::right, true }));

    return list;
}

} // namespace keyla::core
