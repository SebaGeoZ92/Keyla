#include "HarmonyFromAudio.h"

#include "../music/Pitch.h"
#include "../text/Utf8.h"

#include <algorithm>
#include <cmath>

namespace keyla::core
{

using keyla::operator""_u8;

namespace
{
    /** Las calidades que se buscan en audio.

        Menos que las que reconoce `ChordRecognizer` sobre MIDI, y a propósito.
        En MIDI las notas son un hecho y se puede afinar el diagnóstico; aquí
        cada plantilla que se añade es una manera más de que el ruido encaje en
        algo. Las que quedan fuera —séptimas disminuidas, semidisminuidos— se
        confunden demasiado con las que quedan dentro para afirmarlas sobre una
        mezcla. */
    const std::vector<ChordQuality>& audioQualities()
    {
        static const std::vector<ChordQuality> qualities {
            ChordQuality::major, ChordQuality::minor,
            ChordQuality::dominant7, ChordQuality::minor7, ChordQuality::major7,
            ChordQuality::diminished, ChordQuality::augmented,
            ChordQuality::sus2, ChordQuality::sus4,
            ChordQuality::major6, ChordQuality::minor6
        };

        return qualities;
    }

    /** Perfiles de Krumhansl y Kessler: cuánto "pertenece" cada nota a una
        tonalidad, medido preguntando a oyentes reales en los años ochenta. Es
        el método clásico para estimar tonalidad y sigue siendo el que mejor
        relación da entre acierto y coste. */
    constexpr double majorProfile[12] = {
        6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88
    };

    constexpr double minorProfile[12] = {
        6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17
    };

    double correlate (const std::array<double, 12>& values, const double* profile, int rotation)
    {
        double sumA = 0.0, sumB = 0.0;

        for (int i = 0; i < 12; ++i)
        {
            sumA += values[static_cast<std::size_t> (i)];
            sumB += profile[i];
        }

        const double meanA = sumA / 12.0;
        const double meanB = sumB / 12.0;

        double numerator = 0.0, varA = 0.0, varB = 0.0;

        for (int i = 0; i < 12; ++i)
        {
            const double a = values[static_cast<std::size_t> ((i + rotation) % 12)] - meanA;
            const double b = profile[i] - meanB;

            numerator += a * b;
            varA += a * a;
            varB += b * b;
        }

        const double denominator = std::sqrt (varA * varB);
        return denominator > 0.0 ? numerator / denominator : 0.0;
    }
}

AudioChordEstimate HarmonyListener::estimate (const Chroma& chroma,
                                              int bassPitchClass,
                                              const Options& options)
{
    AudioChordEstimate result;
    result.bassPitchClass = bassPitchClass;

    if (chroma.isSilent())
        return result;

    // Agudizar: hunde los armónicos parásitos frente a las notas tocadas.
    std::array<double, 12> sharpened {};
    double norm = 0.0;

    for (int i = 0; i < 12; ++i)
    {
        const double value = std::pow (std::max (0.0, chroma.bins[static_cast<std::size_t> (i)]),
                                       options.sharpening);
        sharpened[static_cast<std::size_t> (i)] = value;
        norm += value * value;
    }

    norm = std::sqrt (norm);

    if (norm <= 0.0)
        return result;

    for (auto& value : sharpened)
        value /= norm;

    double best = -1.0;
    double runnerUp = -1.0;
    int bestRoot = -1;
    ChordQuality bestQuality = ChordQuality::unknown;

    for (auto quality : audioQualities())
    {
        const auto intervals = ChordRecognizer::intervalsFor (quality);

        if (intervals.empty())
            continue;

        // La plantilla se normaliza también, de modo que el coseno no premia a
        // los acordes de cuatro notas por el mero hecho de tener más.
        const double templateNorm = std::sqrt (static_cast<double> (intervals.size()));

        for (int root = 0; root < 12; ++root)
        {
            double score = 0.0;

            for (auto interval : intervals)
                score += sharpened[static_cast<std::size_t> ((root + interval) % 12)];

            score /= templateNorm;

            // El bajo manda: desempata entre lecturas que comparten notas.
            if (root == bassPitchClass)
                score += options.bassIsRootBonus;

            const bool isSeventh = quality == ChordQuality::dominant7
                                || quality == ChordQuality::minor7
                                || quality == ChordQuality::major7;

            if (isSeventh)
                score -= options.seventhQualityPenalty;
            else if (quality != ChordQuality::major && quality != ChordQuality::minor)
                score -= options.complexQualityPenalty;

            if (score > best)
            {
                runnerUp = best;
                best = score;
                bestRoot = root;
                bestQuality = quality;
            }
            else if (score > runnerUp)
            {
                runnerUp = score;
            }
        }
    }

    if (bestRoot < 0)
        return result;

    result.confidence = best;
    result.margin = best - std::max (0.0, runnerUp);
    result.rootPitchClass = bestRoot;
    result.quality = bestQuality;

    result.recognised = best >= options.minConfidence
                     && result.margin >= options.minMargin;

    if (! result.recognised)
        return result;

    result.symbol = pitchClassName (bestRoot) + ChordRecognizer::qualitySymbol (bestQuality);

    // El bajo manda, igual que en el reconocedor de MIDI: si la nota más grave
    // no es la fundamental, se cifra con barra. Es la misma regla y tiene que
    // serlo, porque el alumno ve los dos cifrados en la misma ventana.
    if (bassPitchClass >= 0 && bassPitchClass != bestRoot)
        result.symbol += "/" + pitchClassName (bassPitchClass);

    result.description = pitchClassName (bestRoot) + " "
                       + ChordRecognizer::qualityDescription (bestQuality);

    return result;
}

namespace
{
    enum class Family { major, minor, diminished, other };

    Family familyOf (ChordQuality quality)
    {
        switch (quality)
        {
            case ChordQuality::major:
            case ChordQuality::dominant7:
            case ChordQuality::major7:
            case ChordQuality::major6:
                return Family::major;

            case ChordQuality::minor:
            case ChordQuality::minor7:
            case ChordQuality::minor6:
            case ChordQuality::minorMajor7:
                return Family::minor;

            case ChordQuality::diminished:
            case ChordQuality::halfDiminished7:
            case ChordQuality::diminished7:
                return Family::diminished;

            default:
                return Family::other;
        }
    }

    struct Degree
    {
        int semitones;
        Family family;
    };

    // Los acordes que pertenecen a cada modo. En menor entran los dos quintos
    // grados, el natural y el armónico, porque en música popular los dos son
    // igual de normales.
    const Degree majorDegrees[] = {
        { 0, Family::major }, { 2, Family::minor }, { 4, Family::minor }, { 5, Family::major },
        { 7, Family::major }, { 9, Family::minor }, { 11, Family::diminished }
    };

    const Degree minorDegrees[] = {
        { 0, Family::minor }, { 2, Family::diminished }, { 3, Family::major }, { 5, Family::minor },
        { 7, Family::minor }, { 7, Family::major }, { 8, Family::major }, { 10, Family::major }
    };

    bool belongs (const ChordDuration& chord, int tonic, bool minor)
    {
        const int interval = ((chord.rootPitchClass - tonic) % 12 + 12) % 12;
        const auto family = familyOf (chord.quality);

        if (minor)
        {
            for (const auto& degree : minorDegrees)
                if (degree.semitones == interval && degree.family == family)
                    return true;
        }
        else
        {
            for (const auto& degree : majorDegrees)
                if (degree.semitones == interval && degree.family == family)
                    return true;
        }

        return false;
    }
}

KeyEstimate keyFromChordDurations (const std::vector<ChordDuration>& chords)
{
    KeyEstimate result;

    double total = 0.0;

    for (const auto& chord : chords)
        if (chord.rootPitchClass >= 0)
            total += chord.seconds;

    if (total <= 0.0)
        return result;

    double bestScore = -1.0;
    double bestFit = 0.0;

    for (int tonic = 0; tonic < 12; ++tonic)
    {
        for (int minor = 0; minor < 2; ++minor)
        {
            double fit = 0.0;
            double tonicTime = 0.0;
            double dominantTime = 0.0;

            for (const auto& chord : chords)
            {
                if (chord.rootPitchClass < 0 || ! belongs (chord, tonic, minor != 0))
                    continue;

                fit += chord.seconds;

                const auto family = familyOf (chord.quality);

                if (chord.rootPitchClass == tonic
                    && family == (minor != 0 ? Family::minor : Family::major))
                    tonicTime += chord.seconds;

                // El quinto grado. En menor vale el mayor —el dominante de
                // verdad— y también el menor, que en música popular es igual
                // de frecuente.
                if (chord.rootPitchClass == (tonic + 7) % 12
                    && (family == Family::major || (minor != 0 && family == Family::minor)))
                    dominantTime += chord.seconds;
            }

            // Encajar manda; la tónica y el dominante sólo desempatan.
            //
            // Hace falta desempatar porque dos tonalidades relativas comparten
            // **todos** sus acordes: Do-Sol-Lam-Fa es igual de Do mayor que de
            // La menor. Mirar sólo qué tónica ha sonado más es tirar una moneda
            // cuando suenan lo mismo. Lo que resuelve el empate es lo que miraría
            // un músico: si está el dominante. En Do mayor el Sol es el quinto
            // grado y suena; en La menor el dominante sería Mi, y no aparece.
            //
            // La tónica pesa el doble que el dominante: una canción que se
            // queda en Lam el doble de tiempo que en cualquier otro acorde está
            // en La menor aunque tenga un Sol.
            const double score = fit + 0.5 * tonicTime + 0.25 * dominantTime;

            if (score > bestScore)
            {
                bestScore = score;
                bestFit = fit;
                result.tonicPitchClass = tonic;
                result.minor = minor != 0;
            }
        }
    }

    result.confidence = bestFit / total;

    // Si menos del 60 % del tiempo encaja en la tonalidad elegida, lo que suena
    // no tiene una tonalidad clara, o el reconocedor se equivoca demasiado para
    // deducirla. En los dos casos, mejor callar.
    result.recognised = result.confidence >= 0.6;

    if (result.recognised)
        result.name = pitchClassName (result.tonicPitchClass)
                    + (result.minor ? " menor" : " mayor");

    return result;
}

void HarmonyListener::reset()
{
    stable = AudioChordEstimate {};
    candidate = AudioChordEstimate {};
    agreement = 0;
    keyAccumulator = {};
    chordFrames = {};
    history.clear();
}

AudioChordEstimate HarmonyListener::observe (const Chroma& chroma,
                                             const std::vector<double>& pitchProfile,
                                             int lowestPitch)
{
    if (chroma.isSilent())
    {
        agreement = 0;
        return stable;
    }

    // El bajo: la altura sonando más grave que llega al umbral. Se busca sobre
    // el perfil por altura y no sobre el cromagrama porque el cromagrama ya ha
    // plegado las octavas y ahí la palabra "grave" no significa nada.
    int bassPitchClass = -1;

    if (! pitchProfile.empty())
    {
        const double peak = *std::max_element (pitchProfile.begin(), pitchProfile.end());
        const double threshold = peak * opts.bassThreshold;

        for (std::size_t i = 0; i < pitchProfile.size(); ++i)
        {
            if (pitchProfile[i] >= threshold)
            {
                bassPitchClass = pitchClassOf (lowestPitch + static_cast<int> (i));
                break;
            }
        }
    }

    const auto frame = estimate (chroma, bassPitchClass, opts);

    // La tonalidad se acumula sobre todo lo oído, no sobre el fotograma: una
    // tonalidad es una propiedad de la canción entera.
    for (int i = 0; i < 12; ++i)
        keyAccumulator[static_cast<std::size_t> (i)] += chroma.bins[static_cast<std::size_t> (i)];

    // Cada fotograma cuenta para el acorde que está sonando en ese momento, se
    // haya reconocido éste o no: la tonalidad es cuánto tiempo pasa la canción
    // en cada acorde, no cuántas veces se reconoció.
    if (stable.recognised && stable.rootPitchClass >= 0)
        chordFrames[static_cast<std::size_t> (stable.rootPitchClass * 16
                                              + static_cast<int> (stable.quality))] += 1.0;

    if (! frame.recognised)
    {
        agreement = 0;
        return stable;
    }

    if (frame.rootPitchClass == candidate.rootPitchClass && frame.quality == candidate.quality)
    {
        ++agreement;
    }
    else
    {
        candidate = frame;
        agreement = 1;
    }

    if (agreement >= opts.framesToAgree)
    {
        const bool changed = frame.rootPitchClass != stable.rootPitchClass
                          || frame.quality != stable.quality;

        stable = frame;

        if (changed)
            history.push_back (frame);
    }

    return stable;
}

KeyEstimate HarmonyListener::key() const
{
    // En cuanto hay unos segundos de acordes reconocidos, la tonalidad sale de
    // ellos. El perfil de notas queda sólo para el principio, cuando todavía no
    // se ha reconocido nada con qué razonar.
    std::vector<ChordDuration> durations;
    double chordTotal = 0.0;

    for (int root = 0; root < 12; ++root)
        for (int quality = 0; quality < 16; ++quality)
        {
            const double frames = chordFrames[static_cast<std::size_t> (root * 16 + quality)];

            if (frames > 0.0)
            {
                durations.push_back ({ root, static_cast<ChordQuality> (quality), frames });
                chordTotal += frames;
            }
        }

    // Treinta fotogramas son unos tres segundos: menos que eso son dos o tres
    // acordes, y con dos acordes caben demasiadas tonalidades.
    if (chordTotal >= 30.0)
    {
        const auto fromChords = keyFromChordDurations (durations);

        if (fromChords.recognised)
            return fromChords;
    }

    KeyEstimate result;

    double total = 0.0;

    for (auto value : keyAccumulator)
        total += value;

    if (total <= 0.0)
        return result;

    double best = -2.0;

    for (int tonic = 0; tonic < 12; ++tonic)
    {
        for (int minor = 0; minor < 2; ++minor)
        {
            const double score = correlate (keyAccumulator,
                                            minor ? minorProfile : majorProfile,
                                            tonic);

            if (score > best)
            {
                best = score;
                result.tonicPitchClass = tonic;
                result.minor = minor != 0;
            }
        }
    }

    result.confidence = best;

    // 0,5 de correlación es el punto a partir del cual la tonalidad deja de ser
    // una coincidencia. Por debajo se calla: decir "Sol mayor" de un fragmento
    // que no tiene tonalidad clara es peor que no decir nada.
    result.recognised = best >= 0.5;

    if (result.recognised)
        result.name = pitchClassName (result.tonicPitchClass)
                    + (result.minor ? " menor" : " mayor");

    return result;
}

} // namespace keyla::core
