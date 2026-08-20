#include "Metrics.h"

#include "../text/Utf8.h"

#include <algorithm>
#include <cmath>

namespace keyla::core
{

using keyla::operator""_u8;

namespace
{
    double mean (const std::vector<double>& values)
    {
        if (values.empty())
            return 0.0;

        double sum = 0.0;

        for (auto value : values)
            sum += value;

        return sum / static_cast<double> (values.size());
    }

    double standardDeviation (const std::vector<double>& values)
    {
        if (values.size() < 2)
            return 0.0;

        const double average = mean (values);
        double accumulated = 0.0;

        for (auto value : values)
            accumulated += (value - average) * (value - average);

        return std::sqrt (accumulated / static_cast<double> (values.size() - 1));
    }

    /** Pendiente por mínimos cuadrados de y sobre x. Es la deriva: si el error
        crece a lo largo del pasaje, estás frenando. */
    double regressionSlope (const std::vector<double>& x, const std::vector<double>& y)
    {
        if (x.size() != y.size() || x.size() < 3)
            return 0.0;

        const double meanX = mean (x);
        const double meanY = mean (y);

        double numerator = 0.0;
        double denominator = 0.0;

        for (std::size_t i = 0; i < x.size(); ++i)
        {
            const double dx = x[i] - meanX;
            numerator += dx * (y[i] - meanY);
            denominator += dx * dx;
        }

        return denominator > 0.0 ? numerator / denominator : 0.0;
    }
}

PerformanceMetrics computeMetrics (const Alignment& alignment,
                                   const std::vector<PlayedEvent>& played,
                                   double sampleRate)
{
    PerformanceMetrics metrics;

    if (sampleRate <= 0.0)
        sampleRate = 48000.0;

    std::vector<double> errors;         // ms
    std::vector<double> times;          // s, para la regresión
    std::vector<double> velocities;

    for (const auto& pair : alignment.pairs)
    {
        if (pair.expectedIndex >= 0)
            ++metrics.totalExpected;

        switch (pair.label)
        {
            case NoteLabel::omitted:     ++metrics.omitted;      break;
            case NoteLabel::extra:       ++metrics.extra;        break;
            case NoteLabel::wrongPitch:  ++metrics.wrongPitches; break;
            default:                     ++metrics.correctPitches; break;
        }

        if (pair.playedIndex >= 0)
            ++metrics.played;

        // Sólo entran en las métricas temporales las notas emparejadas **con la
        // altura correcta**: medir el timing de una nota equivocada es medir el
        // tiempo de algo que no se pidió.
        const bool usableForTiming = pair.expectedIndex >= 0
                                  && pair.playedIndex >= 0
                                  && pair.label != NoteLabel::wrongPitch;

        if (! usableForTiming)
            continue;

        const auto index = static_cast<std::size_t> (pair.playedIndex);

        if (index >= played.size())
            continue;

        errors.push_back (pair.timingErrorMs);
        times.push_back (played[index].onsetSample() / sampleRate);
        velocities.push_back (played[index].meanVelocity());
    }

    metrics.timingSampleCount = static_cast<int> (errors.size());
    metrics.timingBiasMs = mean (errors);
    metrics.consistencyMs = standardDeviation (errors);
    metrics.tempoDriftMsPerSecond = regressionSlope (times, errors);
    metrics.meanVelocity = mean (velocities);
    metrics.velocitySpread = standardDeviation (velocities);

    // Regularidad: σ de los intervalos entre ataques consecutivos, sobre **todo**
    // lo tocado. A diferencia del resto, no necesita saber qué se esperaba: en
    // una escala es lo más honesto que se le puede decir a alguien.
    if (played.size() >= 3)
    {
        std::vector<double> intervals;

        for (std::size_t i = 1; i < played.size(); ++i)
            intervals.push_back ((played[i].onsetSample() - played[i - 1].onsetSample())
                                 / sampleRate * 1000.0);

        metrics.regularityMs = standardDeviation (intervals);
    }

    return metrics;
}

juce::StringArray describeMetrics (const PerformanceMetrics& metrics)
{
    juce::StringArray lines;

    if (metrics.totalExpected == 0)
        return lines;

    // ── Lo primero, las notas: sin las alturas bien, el ritmo no importa ────
    if (metrics.wrongPitches > 0 || metrics.omitted > 0 || metrics.extra > 0)
    {
        juce::StringArray problems;

        if (metrics.wrongPitches > 0)
            problems.add (juce::String (metrics.wrongPitches) + " con la altura equivocada"_u8);

        if (metrics.omitted > 0)
            problems.add (juce::String (metrics.omitted) + " sin tocar");

        if (metrics.extra > 0)
            problems.add (juce::String (metrics.extra) + " de más"_u8);

        lines.add ("Notas: " + problems.joinIntoString (", ") + ".");
    }
    else
    {
        lines.add ("Todas las notas correctas."_u8);
    }

    if (! metrics.hasTimingData())
    {
        // Con cuatro notas una desviación típica no significa nada, y darla
        // como si significara algo es peor que no darla.
        lines.add ("Muy pocas notas para decir nada del ritmo."_u8);
        return lines;
    }

    // ── Sesgo: constante y corregible ───────────────────────────────────────
    const double bias = metrics.timingBiasMs;

    if (std::abs (bias) >= 15.0)
        lines.add (bias < 0.0
                       ? "Te adelantas de forma constante, "_u8 + juce::String (-bias, 0)
                             + " ms de media. Es un hábito, no un descuido: escucha el clic antes de entrar."_u8
                       : "Vas por detrás de forma constante, "_u8 + juce::String (bias, 0)
                             + " ms de media."_u8);

    // ── Consistencia: la métrica de músico ──────────────────────────────────
    if (metrics.consistencyMs < 20.0)
        lines.add ("Muy regular: "_u8 + juce::String (metrics.consistencyMs, 0)
                   + " ms de dispersión. Es la señal de que lo tienes."_u8);
    else if (metrics.consistencyMs < 45.0)
        lines.add ("Dispersión de "_u8 + juce::String (metrics.consistencyMs, 0)
                   + " ms. Vas bien; la regularidad mejora antes que la velocidad."_u8);
    else
        lines.add ("Dispersión de "_u8 + juce::String (metrics.consistencyMs, 0)
                   + " ms: cada nota cae en un sitio distinto. Baja el tempo hasta que se estabilice."_u8);

    // ── Deriva: lo que el error medio esconde ───────────────────────────────
    const double drift = metrics.tempoDriftMsPerSecond;

    if (std::abs (drift) >= 4.0)
        lines.add (drift > 0.0
                       ? "Vas frenando según avanzas."_u8
                       : "Vas acelerando según avanzas."_u8);

    // ── Dedos débiles ───────────────────────────────────────────────────────
    if (metrics.velocitySpread >= 22.0)
        lines.add ("Las notas no suenan igual de fuertes ("_u8
                   + juce::String (metrics.velocitySpread, 0)
                   + " de dispersión). Suele ser el anular y el meñique."_u8);

    return lines;
}

} // namespace keyla::core
