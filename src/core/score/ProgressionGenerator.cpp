#include "ProgressionGenerator.h"

#include "../music/Pitch.h"
#include "../text/Utf8.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace keyla::core
{

using keyla::operator""_u8;

std::vector<ProgressionStep> progressionSteps (ProgressionId id, bool useSevenths)
{
    const auto major = useSevenths ? ChordQuality::major7 : ChordQuality::major;
    const auto minor = useSevenths ? ChordQuality::minor7 : ChordQuality::minor;

    // La dominante lleva séptima aunque no se pidan séptimas en el resto: es
    // lo que le da la tensión que resuelve, y sin ella una cadencia no suena a
    // cadencia.
    const auto dominant = useSevenths ? ChordQuality::dominant7 : ChordQuality::major;

    switch (id)
    {
        case ProgressionId::popIVviIV:
            return { { 0, major, "I" }, { 7, dominant, "V" },
                     { 9, minor, "vi" }, { 5, major, "IV" } };

        case ProgressionId::doowop:
            return { { 0, major, "I" }, { 9, minor, "vi" },
                     { 5, major, "IV" }, { 7, dominant, "V" } };

        case ProgressionId::twoFiveOne:
            return { { 2, ChordQuality::minor7, "ii7" },
                     { 7, ChordQuality::dominant7, "V7" },
                     { 0, ChordQuality::major7, "Imaj7" } };

        case ProgressionId::canon:
            return { { 0, major, "I" }, { 7, dominant, "V" }, { 9, minor, "vi" },
                     { 4, minor, "iii" }, { 5, major, "IV" }, { 0, major, "I" },
                     { 5, major, "IV" }, { 7, dominant, "V" } };

        case ProgressionId::minorPop:
            return { { 0, minor, "i" }, { 8, major, "VI" },
                     { 3, major, "III" }, { 10, major, "VII" } };

        case ProgressionId::andalusian:
            return { { 0, minor, "i" }, { 10, major, "VII" },
                     { 8, major, "VI" }, { 7, dominant, "V" } };

        case ProgressionId::twelveBarBlues:
            // Los doce compases, con las dominantes que le dan el color.
            return { { 0, ChordQuality::dominant7, "I7" }, { 0, ChordQuality::dominant7, "I7" },
                     { 0, ChordQuality::dominant7, "I7" }, { 0, ChordQuality::dominant7, "I7" },
                     { 5, ChordQuality::dominant7, "IV7" }, { 5, ChordQuality::dominant7, "IV7" },
                     { 0, ChordQuality::dominant7, "I7" }, { 0, ChordQuality::dominant7, "I7" },
                     { 7, ChordQuality::dominant7, "V7" }, { 5, ChordQuality::dominant7, "IV7" },
                     { 0, ChordQuality::dominant7, "I7" }, { 7, ChordQuality::dominant7, "V7" } };

        case ProgressionId::cumbia:
            return { { 0, minor, "i" }, { 5, minor, "iv" },
                     { 7, dominant, "V" }, { 0, minor, "i" } };
    }

    return { { 0, major, "I" } };
}

juce::String progressionName (ProgressionId id)
{
    switch (id)
    {
        case ProgressionId::popIVviIV:       return "Pop"_u8;
        case ProgressionId::doowop:          return "Doo-wop";
        case ProgressionId::twoFiveOne:      return "ii-V-I (jazz)";
        case ProgressionId::canon:           return "Canon"_u8;
        case ProgressionId::minorPop:        return "Pop menor";
        case ProgressionId::andalusian:      return "Cadencia andaluza"_u8;
        case ProgressionId::twelveBarBlues:  return "Blues de 12 compases"_u8;
        case ProgressionId::cumbia:          return "Cumbia";
    }

    return {};
}

juce::String progressionDegrees (ProgressionId id)
{
    juce::StringArray labels;

    for (const auto& step : progressionSteps (id, false))
        labels.add (step.degreeLabel);

    // El blues son doce compases y escribirlos todos no cabe ni se lee.
    if (id == ProgressionId::twelveBarBlues)
        return "I7 · IV7 · V7 sobre 12 compases"_u8;

    return labels.joinIntoString (" - "_u8);
}

juce::String voicingName (Voicing voicing)
{
    switch (voicing)
    {
        case Voicing::rootPosition:       return "en estado fundamental"_u8;
        case Voicing::smoothVoiceLeading: return "con enlace de voces"_u8;
        case Voicing::withLeftHandBass:   return "con bajo en la izquierda"_u8;
    }

    return {};
}

namespace
{
    /** Todas las colocaciones razonables de un acorde: cada inversión, en varias
        octavas alrededor del centro del teclado que se está usando. */
    std::vector<std::vector<int>> candidateVoicings (int rootPitchClass,
                                                     ChordQuality quality,
                                                     int centre)
    {
        const auto intervals = ChordRecognizer::intervalsFor (quality);
        std::vector<std::vector<int>> candidates;

        const auto count = static_cast<int> (intervals.size());

        for (int inversion = 0; inversion < count; ++inversion)
        {
            for (int octave = -1; octave <= 1; ++octave)
            {
                std::vector<int> voicing;

                for (int i = 0; i < count; ++i)
                {
                    const int index = (inversion + i) % count;
                    const bool wrapped = inversion + i >= count;

                    voicing.push_back (rootPitchClass + intervals[static_cast<std::size_t> (index)]
                                       + (wrapped ? 12 : 0));
                }

                // Se sube la colocación entera hasta dejar la nota grave cerca
                // del centro pedido.
                const int lowest = *std::min_element (voicing.begin(), voicing.end());
                const int shift = static_cast<int> (std::round ((centre - lowest) / 12.0)) * 12
                                + octave * 12;

                for (auto& pitch : voicing)
                    pitch += shift;

                if (*std::min_element (voicing.begin(), voicing.end()) < 21
                    || *std::max_element (voicing.begin(), voicing.end()) > 104)
                    continue;

                std::sort (voicing.begin(), voicing.end());
                candidates.push_back (std::move (voicing));
            }
        }

        return candidates;
    }

    /** Cuánto tendría que moverse la mano para pasar de una colocación a otra.

        Se suma la distancia dedo a dedo entre las dos colocaciones ordenadas.
        Cuando no tienen el mismo número de notas —al pasar de tríada a
        cuatríada— se comparan las que hay en común y la nota nueva se penaliza
        un poco: aparecer de la nada también cuesta. */
    int voicingDistance (const std::vector<int>& from, const std::vector<int>& to)
    {
        if (from.empty())
            return 0;

        const auto common = std::min (from.size(), to.size());
        int distance = 0;

        for (std::size_t i = 0; i < common; ++i)
            distance += std::abs (from[i] - to[i]);

        distance += static_cast<int> (std::max (from.size(), to.size()) - common) * 3;

        return distance;
    }
}

std::vector<int> voiceChordNear (int rootPitchClass, ChordQuality quality,
                                 const std::vector<int>& previous, int centrePitch)
{
    const auto candidates = candidateVoicings (pitchClassOf (rootPitchClass), quality, centrePitch);

    if (candidates.empty())
        return {};

    std::size_t best = 0;
    int bestScore = std::numeric_limits<int>::max();

    for (std::size_t i = 0; i < candidates.size(); ++i)
    {
        const auto& candidate = candidates[i];

        // Sin mano anterior no hay nada que enlazar: se elige la colocación
        // que quede mejor centrada, que es donde una mano descansa.
        const int score = previous.empty()
                        ? std::abs (candidate.front() - centrePitch)
                              + std::abs (candidate.back() - centrePitch)
                        : voicingDistance (previous, candidate);

        if (score < bestScore)
        {
            bestScore = score;
            best = i;
        }
    }

    return candidates[best];
}

int bassNoteFor (int rootPitchClass, const std::vector<int>& rightHand)
{
    if (rightHand.empty())
        return -1;

    const int top = *std::min_element (rightHand.begin(), rightHand.end());

    int bass = pitchClassOf (rootPitchClass) + 36;

    // Ni tan lejos que se pierda el contacto entre las manos...
    while (bass < top - 19)
        bass += 12;

    // ...ni tan cerca que se pisen: al menos una quinta por debajo.
    while (bass > top - 7)
        bass -= 12;

    return (bass >= 21 && bass <= 108) ? bass : -1;
}

Exercise generateProgression (const ProgressionRequest& request)
{
    Exercise exercise;

    const auto steps = progressionSteps (request.id, request.useSevenths);
    const int repeats = std::clamp (request.repeats, 1, 8);
    const double beatsPerChord = std::max (1.0, request.beatsPerChord);

    const bool withBass = request.voicing == Voicing::withLeftHandBass;
    const int tonic = std::clamp (request.tonicPitch, 24, 96);

    // Centro de la mano derecha. Con bajo en la izquierda se sube un poco para
    // dejarle sitio abajo y que las dos manos no se pisen.
    const int centre = withBass ? tonic + 4 : tonic;

    std::vector<int> previous;
    double beat = 0.0;

    for (int repeat = 0; repeat < repeats; ++repeat)
    {
        for (const auto& step : steps)
        {
            const int rootPitchClass = pitchClassOf (tonic + step.semitonesFromTonic);

            std::vector<int> chosen;

            if (request.voicing == Voicing::rootPosition)
            {
                const auto intervals = ChordRecognizer::intervalsFor (step.quality);
                const int root = tonic + step.semitonesFromTonic;

                for (auto interval : intervals)
                    chosen.push_back (root + interval);
            }
            else
            {
                const auto candidates = candidateVoicings (rootPitchClass, step.quality, centre);

                if (candidates.empty())
                    continue;

                // La primera vez no hay mano anterior a la que acercarse, así
                // que se coge la que quede más centrada.
                int best = 0;
                int bestScore = std::numeric_limits<int>::max();

                for (std::size_t i = 0; i < candidates.size(); ++i)
                {
                    const int score = previous.empty()
                                    ? std::abs (candidates[i].front() - centre)
                                    : voicingDistance (previous, candidates[i]);

                    if (score < bestScore)
                    {
                        bestScore = score;
                        best = static_cast<int> (i);
                    }
                }

                chosen = candidates[static_cast<std::size_t> (best)];
                previous = chosen;
            }

            std::sort (chosen.begin(), chosen.end());

            ExpectedEvent event;
            event.onsetBeat = beat;
            event.durationBeats = beatsPerChord;
            event.hand = withBass ? Hand::both : request.hand;

            if (withBass)
            {
                // La fundamental abajo, lo bastante lejos de la derecha para
                // que no se solapen las manos.
                int bass = rootPitchClass + 36;

                while (bass < chosen.front() - 19)
                    bass += 12;

                event.pitches.push_back (static_cast<std::uint8_t> (std::clamp (bass, 21, 108)));
            }

            for (auto pitch : chosen)
                event.pitches.push_back (static_cast<std::uint8_t> (std::clamp (pitch, 21, 108)));

            std::sort (event.pitches.begin(), event.pitches.end());
            event.pitches.erase (std::unique (event.pitches.begin(), event.pitches.end()),
                                 event.pitches.end());

            exercise.events.push_back (std::move (event));
            beat += beatsPerChord;
        }
    }

    const auto tonicName = pitchClassName (pitchClassOf (tonic));

    exercise.id = "progression." + tonicName.toLowerCase() + "."
                + juce::String (static_cast<int> (request.id)) + "."
                + juce::String (static_cast<int> (request.voicing));

    // Los grados en minúscula al principio (i, ii...) son tónica menor.
    const bool minorTonic = progressionDegrees (request.id).startsWith ("i");
    const auto spokenTonic = spanishPitchClassName (pitchClassOf (tonic),
                                                    conventionalAccidental (pitchClassOf (tonic), minorTonic));

    exercise.name = progressionName (request.id) + " en "_u8 + spokenTonic
                  + "  ·  "_u8 + progressionDegrees (request.id)
                  + "  ·  "_u8 + voicingName (request.voicing);

    exercise.hint = request.voicing == Voicing::rootPosition
                  ? "Todo en fundamental: mira la forma del acorde antes de saltar."_u8
                  : "Las manos apenas se mueven: busca la nota que se queda quieta entre acorde y acorde."_u8;

    return exercise;
}

} // namespace keyla::core
