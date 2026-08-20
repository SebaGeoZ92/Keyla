#include "OfflineAligner.h"

#include "../text/Utf8.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace keyla::core
{

using keyla::operator""_u8;

juce::String labelName (NoteLabel label)
{
    switch (label)
    {
        case NoteLabel::correct:     return "correcta";
        case NoteLabel::early:       return "adelantada";
        case NoteLabel::late:        return "atrasada";
        case NoteLabel::wrongPitch:  return "altura incorrecta";
        case NoteLabel::omitted:     return "omitida";
        case NoteLabel::extra:       return "adicional";
    }

    return "?";
}

int Alignment::matched() const noexcept
{
    return static_cast<int> (std::count_if (pairs.begin(), pairs.end(),
        [] (const AlignedPair& p) { return p.expectedIndex >= 0 && p.playedIndex >= 0; }));
}

int Alignment::omitted() const noexcept
{
    return static_cast<int> (std::count_if (pairs.begin(), pairs.end(),
        [] (const AlignedPair& p) { return p.label == NoteLabel::omitted; }));
}

int Alignment::extra() const noexcept
{
    return static_cast<int> (std::count_if (pairs.begin(), pairs.end(),
        [] (const AlignedPair& p) { return p.label == NoteLabel::extra; }));
}

int Alignment::wrongPitch() const noexcept
{
    return static_cast<int> (std::count_if (pairs.begin(), pairs.end(),
        [] (const AlignedPair& p) { return p.label == NoteLabel::wrongPitch; }));
}

namespace
{
    /** Cuánto "cuesta" decir que este evento tocado es aquel esperado.

        Mezcla dos cosas de naturaleza distinta en una sola unidad —
        milisegundos equivalentes— porque la distancia de edición necesita un
        único número. Que las alturas distintas cuesten mucho es deliberado:
        antes que casar un Do con un Fa, es más honesto decir que uno se omitió
        y el otro sobró.
    */
    double matchCost (const PlayedEvent& played, const ExpectedEvent& expected,
                      double expectedSeconds, double sampleRate,
                      const AlignmentOptions& options)
    {
        const double playedSeconds = played.onsetSample() / sampleRate;
        const double timingCostMs = std::abs (playedSeconds - expectedSeconds) * 1000.0;

        // Cuántas de las alturas esperadas están de verdad ahí.
        int hits = 0;

        for (auto pitch : expected.pitches)
            if (played.contains (pitch))
                ++hits;

        const auto expectedCount = static_cast<int> (expected.pitches.size());
        const int misses = std::max (0, expectedCount - hits);

        return timingCostMs + misses * options.wrongPitchCostMs;
    }

    /** La altura esperada más cercana a la tocada: es la que el alumno estaba
        buscando, y por tanto la que hace útil el "un semitono abajo". */
    int nearestExpectedPitch (const ExpectedEvent& expected, int playedPitch)
    {
        if (expected.pitches.empty())
            return playedPitch;

        int nearest = expected.pitches.front();

        for (auto pitch : expected.pitches)
            if (std::abs (pitch - playedPitch) < std::abs (nearest - playedPitch))
                nearest = pitch;

        return nearest;
    }
}

Alignment alignPerformance (const std::vector<PlayedEvent>& played,
                            const std::vector<ExpectedEvent>& expected,
                            double sampleRate,
                            double beatsToSeconds,
                            const AlignmentOptions& options)
{
    Alignment alignment;

    const auto numPlayed = static_cast<int> (played.size());
    const auto numExpected = static_cast<int> (expected.size());

    if (numExpected == 0 && numPlayed == 0)
        return alignment;

    // ── Programación dinámica ───────────────────────────────────────────────
    //
    // cost[i][j] = coste mínimo de explicar los primeros i eventos tocados con
    // los primeros j esperados. Tres movimientos: emparejar, omitir el esperado
    // o declarar el tocado como añadido.
    const std::size_t rows = static_cast<std::size_t> (numPlayed) + 1;
    const std::size_t cols = static_cast<std::size_t> (numExpected) + 1;

    std::vector<std::vector<double>> cost (rows, std::vector<double> (cols, 0.0));
    std::vector<std::vector<char>> move (rows, std::vector<char> (cols, ' '));

    for (std::size_t i = 1; i < rows; ++i)
    {
        cost[i][0] = cost[i - 1][0] + options.skipCostMs;
        move[i][0] = 'x';       // tocada de más
    }

    for (std::size_t j = 1; j < cols; ++j)
    {
        cost[0][j] = cost[0][j - 1] + options.skipCostMs;
        move[0][j] = 'o';       // omitida
    }

    for (std::size_t i = 1; i < rows; ++i)
    {
        for (std::size_t j = 1; j < cols; ++j)
        {
            const auto& playedEvent = played[i - 1];
            const auto& expectedEvent = expected[j - 1];
            const double expectedSeconds = expectedEvent.onsetBeat * beatsToSeconds;

            const double matchTotal = cost[i - 1][j - 1]
                                    + matchCost (playedEvent, expectedEvent,
                                                 expectedSeconds, sampleRate, options);
            const double extraTotal = cost[i - 1][j] + options.skipCostMs;
            const double omitTotal = cost[i][j - 1] + options.skipCostMs;

            if (matchTotal <= extraTotal && matchTotal <= omitTotal)
            {
                cost[i][j] = matchTotal;
                move[i][j] = 'm';
            }
            else if (extraTotal <= omitTotal)
            {
                cost[i][j] = extraTotal;
                move[i][j] = 'x';
            }
            else
            {
                cost[i][j] = omitTotal;
                move[i][j] = 'o';
            }
        }
    }

    // ── Reconstrucción del camino ───────────────────────────────────────────
    std::vector<AlignedPair> reversed;

    auto i = static_cast<std::size_t> (numPlayed);
    auto j = static_cast<std::size_t> (numExpected);

    while (i > 0 || j > 0)
    {
        const char step = (i > 0 && j > 0) ? move[i][j] : (i > 0 ? 'x' : 'o');

        AlignedPair pair;

        if (step == 'm')
        {
            const auto& playedEvent = played[i - 1];
            const auto& expectedEvent = expected[j - 1];

            pair.playedIndex = static_cast<int> (i) - 1;
            pair.expectedIndex = static_cast<int> (j) - 1;

            const double expectedSeconds = expectedEvent.onsetBeat * beatsToSeconds;
            const double playedSeconds = playedEvent.onsetSample() / sampleRate;

            // Invariante 7: el offset perceptual se resta **antes** de reportar
            // nada. Sin esto, todo alumno sale tarde por una constante que no
            // es suya.
            pair.timingErrorMs = (playedSeconds - expectedSeconds) * 1000.0
                               - options.perceptualOffsetMs;

            const int expectedPitch = expectedEvent.pitches.empty()
                                    ? 0 : expectedEvent.pitches.front();

            pair.expectedPitch = expectedPitch;

            // ¿Está la altura esperada entre las tocadas?
            bool pitchOk = true;

            for (auto pitch : expectedEvent.pitches)
                if (! playedEvent.contains (pitch))
                    pitchOk = false;

            if (! pitchOk)
            {
                pair.label = NoteLabel::wrongPitch;
                pair.playedPitch = playedEvent.notes.empty() ? 0 : playedEvent.notes.front().pitch;
                const int nearest = nearestExpectedPitch (expectedEvent, pair.playedPitch);
                pair.expectedPitch = nearest;
                pair.semitonesOff = pair.playedPitch - nearest;
            }
            else
            {
                pair.playedPitch = expectedPitch;

                if (pair.timingErrorMs < -options.toleranceMs)
                    pair.label = NoteLabel::early;
                else if (pair.timingErrorMs > options.toleranceMs)
                    pair.label = NoteLabel::late;
                else
                    pair.label = NoteLabel::correct;
            }

            --i;
            --j;
        }
        else if (step == 'x')
        {
            pair.playedIndex = static_cast<int> (i) - 1;
            pair.label = NoteLabel::extra;
            pair.playedPitch = played[i - 1].notes.empty() ? 0 : played[i - 1].notes.front().pitch;
            --i;
        }
        else
        {
            pair.expectedIndex = static_cast<int> (j) - 1;
            pair.label = NoteLabel::omitted;
            pair.expectedPitch = expected[j - 1].pitches.empty() ? 0 : expected[j - 1].pitches.front();
            --j;
        }

        reversed.push_back (pair);
    }

    alignment.pairs.assign (reversed.rbegin(), reversed.rend());
    return alignment;
}

} // namespace keyla::core
