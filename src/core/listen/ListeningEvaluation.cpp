#include "ListeningEvaluation.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>

namespace keyla::core
{

const TimedChord* chordAt (const std::vector<TimedChord>& timeline, double seconds)
{
    // Búsqueda binaria: comparar trescientos ajustes sobre una canción entera
    // hace miles de millones de consultas, y recorrerla entera cada vez las
    // convertía en minutos.
    const auto after = std::upper_bound (timeline.begin(), timeline.end(), seconds,
                                         [] (double t, const TimedChord& c) { return t < c.seconds; });

    return after == timeline.begin() ? nullptr : &*(after - 1);
}

std::vector<TimedChord> chordsFromNotes (const std::vector<TimedNote>& notes,
                                         double minimumHoldSeconds)
{
    auto ordered = notes;

    std::stable_sort (ordered.begin(), ordered.end(),
                      [] (const TimedNote& a, const TimedNote& b) { return a.seconds < b.seconds; });

    std::vector<TimedChord> chords;
    std::set<int> held;

    for (std::size_t i = 0; i < ordered.size(); ++i)
    {
        const auto& note = ordered[i];

        if (note.isOn)
            held.insert (note.pitch);
        else
            held.erase (note.pitch);

        // Varias notas en el mismo instante son un solo gesto: se espera a la
        // última antes de mirar qué hay bajo los dedos.
        if (i + 1 < ordered.size() && ordered[i + 1].seconds - note.seconds < 1.0e-9)
            continue;

        if (held.size() < 3)
            continue;

        const double heldFor = i + 1 < ordered.size()
                             ? ordered[i + 1].seconds - note.seconds
                             : minimumHoldSeconds;

        if (heldFor < minimumHoldSeconds)
            continue;

        const auto match = ChordRecognizer::recognise (std::vector<int> (held.begin(), held.end()));

        if (! match.recognised)
            continue;

        if (! chords.empty()
            && chords.back().rootPitchClass == match.rootPitchClass
            && chords.back().quality == match.quality)
            continue;

        chords.push_back ({ note.seconds, match.rootPitchClass, match.quality });
    }

    return chords;
}

namespace
{
    struct Tally
    {
        double compared { 0.0 };
        double rootHits { 0.0 };
        double fullHits { 0.0 };
    };

    Tally tallyAt (const std::vector<TimedChord>& heard,
                   const std::vector<TimedChord>& played,
                   double start, double duration, double lag, double step)
    {
        Tally tally;

        for (double t = start; t < duration; t += step)
        {
            // Lo que Keyla oyó en `t` se compara con lo que tocaste en `t + lag`.
            const auto* h = chordAt (heard, t);
            const auto* p = chordAt (played, t + lag);

            if (h == nullptr || p == nullptr || t + lag >= duration)
                continue;

            tally.compared += step;

            if (h->rootPitchClass == p->rootPitchClass)
            {
                tally.rootHits += step;

                if (h->quality == p->quality)
                    tally.fullHits += step;
            }
        }

        return tally;
    }
}

ListeningEvaluation evaluateListening (const std::vector<TimedChord>& heard,
                                       const std::vector<TimedChord>& played,
                                       double durationSeconds,
                                       double maxLagSeconds,
                                       double stepSeconds,
                                       double startSeconds)
{
    ListeningEvaluation result;

    if (heard.empty() || played.empty() || durationSeconds <= 0.0 || stepSeconds <= 0.0)
        return result;

    double bestLag = 0.0;
    Tally best;
    double bestScore = -1.0;

    const int lagSteps = static_cast<int> (std::round (maxLagSeconds / stepSeconds));

    for (int i = -lagSteps; i <= lagSteps; ++i)
    {
        const double lag = i * stepSeconds;
        const auto tally = tallyAt (heard, played, startSeconds, durationSeconds, lag, stepSeconds);

        if (tally.compared <= 0.0)
            continue;

        // Se elige por fracción de fundamentales acertadas, y a igualdad, el
        // desfase más pequeño: entre dos explicaciones igual de buenas, la que
        // supone menos retraso es la más probable.
        const double score = tally.rootHits / tally.compared;

        if (score > bestScore + 1.0e-9
            || (std::abs (score - bestScore) <= 1.0e-9 && std::abs (lag) < std::abs (bestLag)))
        {
            bestScore = score;
            bestLag = lag;
            best = tally;
        }
    }

    if (best.compared <= 0.0)
        return result;

    result.valid = true;
    result.lagSeconds = bestLag;
    result.comparedSeconds = best.compared;
    result.rootAgreement = best.rootHits / best.compared;
    result.fullAgreement = best.fullHits / best.compared;

    std::map<std::tuple<int, int, int, int>, double> confusionTime;

    for (double t = startSeconds; t < durationSeconds; t += stepSeconds)
    {
        const auto* h = chordAt (heard, t);
        const auto* p = chordAt (played, t + bestLag);

        if (h == nullptr || p == nullptr || t + bestLag >= durationSeconds)
            continue;

        if (h->rootPitchClass == p->rootPitchClass && h->quality == p->quality)
            continue;

        confusionTime[{ p->rootPitchClass, static_cast<int> (p->quality),
                        h->rootPitchClass, static_cast<int> (h->quality) }] += stepSeconds;
    }

    for (const auto& [key, seconds] : confusionTime)
    {
        ChordConfusion confusion;
        confusion.playedRoot = std::get<0> (key);
        confusion.playedQuality = static_cast<ChordQuality> (std::get<1> (key));
        confusion.heardRoot = std::get<2> (key);
        confusion.heardQuality = static_cast<ChordQuality> (std::get<3> (key));
        confusion.seconds = seconds;
        result.confusions.push_back (confusion);
    }

    std::sort (result.confusions.begin(), result.confusions.end(),
               [] (const ChordConfusion& a, const ChordConfusion& b) { return a.seconds > b.seconds; });

    return result;
}

} // namespace keyla::core
