#include "ChordRecognizer.h"

#include "../text/Utf8.h"

#include <algorithm>
#include <array>

namespace keyla::core
{

using keyla::operator""_u8;

namespace
{
    struct Template
    {
        ChordQuality quality;
        std::array<int, 4> intervals;   // semitonos desde la fundamental
        int size;
    };

    /** El orden importa: se prueba de más notas a menos, para que un Do7 no se
        reconozca como Do mayor ignorando la séptima. */
    const Template templates[] = {
        { ChordQuality::diminished7,     { 0, 3, 6, 9 },  4 },
        { ChordQuality::halfDiminished7, { 0, 3, 6, 10 }, 4 },
        { ChordQuality::minor7,          { 0, 3, 7, 10 }, 4 },
        { ChordQuality::minorMajor7,     { 0, 3, 7, 11 }, 4 },
        { ChordQuality::dominant7,       { 0, 4, 7, 10 }, 4 },
        { ChordQuality::major7,          { 0, 4, 7, 11 }, 4 },
        { ChordQuality::major6,          { 0, 4, 7, 9 },  4 },
        { ChordQuality::minor6,          { 0, 3, 7, 9 },  4 },
        { ChordQuality::major,           { 0, 4, 7, 0 },  3 },
        { ChordQuality::minor,           { 0, 3, 7, 0 },  3 },
        { ChordQuality::diminished,      { 0, 3, 6, 0 },  3 },
        { ChordQuality::augmented,       { 0, 4, 8, 0 },  3 },
        { ChordQuality::sus4,            { 0, 5, 7, 0 },  3 },
        { ChordQuality::sus2,            { 0, 2, 7, 0 },  3 },
    };

    /** Un acorde con fundamental en Fa♯ se escribe con sostenidos; uno en Si♭,
        con bemoles. No es teoría completa, pero acierta en la inmensa mayoría
        de lo que alguien toca practicando. */
    Accidental accidentalForRoot (int rootPitchClass, ChordQuality quality)
    {
        const bool minorish = quality == ChordQuality::minor
                           || quality == ChordQuality::minor7
                           || quality == ChordQuality::minor6
                           || quality == ChordQuality::minorMajor7
                           || quality == ChordQuality::diminished
                           || quality == ChordQuality::diminished7
                           || quality == ChordQuality::halfDiminished7;

        switch (rootPitchClass)
        {
            case 1:  return minorish ? Accidental::sharps : Accidental::flats;   // C#m / Db
            case 3:  return Accidental::flats;                                   // Eb
            case 6:  return Accidental::sharps;                                  // F#
            case 8:  return Accidental::flats;                                   // Ab
            case 10: return Accidental::flats;                                   // Bb
            default: return Accidental::sharps;
        }
    }

    juce::String spanishNoteName (int pitchClass, Accidental accidental)
    {
        static const char* const naturals[7] = { "Do", "Re", "Mi", "Fa", "Sol", "La", "Si" };
        static const int naturalClasses[7]   = { 0, 2, 4, 5, 7, 9, 11 };

        const int index = ((pitchClass % 12) + 12) % 12;

        for (int i = 0; i < 7; ++i)
            if (naturalClasses[i] == index)
                return juce::String (juce::CharPointer_UTF8 (naturals[i]));

        if (accidental == Accidental::flats)
        {
            for (int i = 0; i < 7; ++i)
                if (naturalClasses[i] == ((index + 1) % 12))
                    return juce::String (juce::CharPointer_UTF8 (naturals[i])) + " bemol"_u8;
        }

        for (int i = 6; i >= 0; --i)
            if (naturalClasses[i] == ((index + 11) % 12))
                return juce::String (juce::CharPointer_UTF8 (naturals[i])) + " sostenido"_u8;

        return pitchClassName (index, accidental);
    }

    juce::String inversionDescription (int inversion)
    {
        switch (inversion)
        {
            case 0:  return {};
            case 1:  return ", primera inversión"_u8;
            case 2:  return ", segunda inversión"_u8;
            case 3:  return ", tercera inversión"_u8;
            default: return {};
        }
    }
}

juce::String ChordRecognizer::qualitySymbol (ChordQuality quality)
{
    switch (quality)
    {
        case ChordQuality::major:            return "";
        case ChordQuality::minor:            return "m";
        case ChordQuality::diminished:       return "dim";
        case ChordQuality::augmented:        return "aug";
        case ChordQuality::sus2:             return "sus2";
        case ChordQuality::sus4:             return "sus4";
        case ChordQuality::major6:           return "6";
        case ChordQuality::minor6:           return "m6";
        case ChordQuality::dominant7:        return "7";
        case ChordQuality::major7:           return "maj7";
        case ChordQuality::minor7:           return "m7";
        case ChordQuality::minorMajor7:      return "m(maj7)";
        case ChordQuality::diminished7:      return "dim7";
        case ChordQuality::halfDiminished7:  return "m7b5";
        case ChordQuality::unknown:          break;
    }

    return "?";
}

juce::String ChordRecognizer::qualityDescription (ChordQuality quality)
{
    switch (quality)
    {
        case ChordQuality::major:            return "mayor";
        case ChordQuality::minor:            return "menor";
        case ChordQuality::diminished:       return "disminuido";
        case ChordQuality::augmented:        return "aumentado";
        case ChordQuality::sus2:             return "con segunda suspendida";
        case ChordQuality::sus4:             return "con cuarta suspendida";
        case ChordQuality::major6:           return "mayor con sexta";
        case ChordQuality::minor6:           return "menor con sexta";
        case ChordQuality::dominant7:        return "séptima de dominante"_u8;
        case ChordQuality::major7:           return "séptima mayor"_u8;
        case ChordQuality::minor7:           return "menor séptima"_u8;
        case ChordQuality::minorMajor7:      return "menor con séptima mayor"_u8;
        case ChordQuality::diminished7:      return "séptima disminuida"_u8;
        case ChordQuality::halfDiminished7:  return "semidisminuido"_u8;
        case ChordQuality::unknown:          break;
    }

    return "desconocido";
}

ChordMatch ChordRecognizer::recognise (const std::vector<int>& notes)
{
    ChordMatch match;

    if (notes.empty())
        return match;

    // Clases de altura únicas y ordenadas. Doblar la fundamental una octava
    // arriba no cambia el acorde, así que las octavas se colapsan.
    std::vector<int> classes;
    classes.reserve (notes.size());

    for (auto note : notes)
        classes.push_back (pitchClassOf (note));

    std::sort (classes.begin(), classes.end());
    classes.erase (std::unique (classes.begin(), classes.end()), classes.end());

    match.bassPitchClass = pitchClassOf (*std::min_element (notes.begin(), notes.end()));

    if (classes.size() < 3 || classes.size() > 4)
        return match;       // menos de tres no es un acorde; más de cuatro, aquí no

    // Muchos conjuntos de notas admiten dos lecturas igual de válidas: Do-Re-Sol
    // es Csus2 y también Gsus4/C; Do-Mi-Sol-La es C6 y también Am7/C. Son las
    // mismas teclas y no hay forma de elegir sin conocer la tonalidad.
    //
    // La regla, y es una decisión de producto: **manda el bajo**. Si la mano
    // izquierda está en Do, eso es un acorde de Do. Por eso la fundamental
    // candidata se prueba empezando por la nota más grave, y sólo si con ella
    // no encaja nada se buscan inversiones.
    std::vector<int> candidateRoots;
    candidateRoots.reserve (classes.size());
    candidateRoots.push_back (match.bassPitchClass);

    for (auto pitchClass : classes)
        if (pitchClass != match.bassPitchClass)
            candidateRoots.push_back (pitchClass);

    for (auto candidateRoot : candidateRoots)
    {
        for (const auto& templ : templates)
        {
            if (static_cast<int> (classes.size()) != templ.size)
                continue;

            std::vector<int> relative;
            relative.reserve (classes.size());

            for (auto pitchClass : classes)
                relative.push_back (((pitchClass - candidateRoot) % 12 + 12) % 12);

            std::sort (relative.begin(), relative.end());

            bool matches = true;

            for (int i = 0; i < templ.size; ++i)
            {
                if (relative[static_cast<std::size_t> (i)] != templ.intervals[static_cast<std::size_t> (i)])
                {
                    matches = false;
                    break;
                }
            }

            if (! matches)
                continue;

            match.recognised = true;
            match.rootPitchClass = candidateRoot;
            match.quality = templ.quality;

            // Qué grado del acorde está en el bajo.
            const int bassInterval = ((match.bassPitchClass - candidateRoot) % 12 + 12) % 12;

            for (int i = 0; i < templ.size; ++i)
                if (templ.intervals[static_cast<std::size_t> (i)] == bassInterval)
                    match.inversion = i;

            const auto accidental = accidentalForRoot (candidateRoot, templ.quality);

            match.symbol = pitchClassName (candidateRoot, accidental) + qualitySymbol (templ.quality);

            if (match.inversion != 0)
                match.symbol += "/" + pitchClassName (match.bassPitchClass, accidental);

            match.description = spanishNoteName (candidateRoot, accidental)
                              + " " + qualityDescription (templ.quality)
                              + inversionDescription (match.inversion);

            return match;
        }
    }

    return match;
}

} // namespace keyla::core
