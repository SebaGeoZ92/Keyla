#pragma once

#include <cstdint>

namespace keyla::core
{

/** Compás musical: numerador / denominador (4/4, 6/8, 3/4...).

    **Convenio de pulso, fijado aquí para todo el proyecto:** un *beat* es
    siempre una negra, y el tempo en BPM son negras por minuto. No es una
    elección arbitraria — es lo que guarda un Standard MIDI File (microsegundos
    por negra), y el SMF es el primer formato que se importa (doc 02 §6).
    Definirlo de otra forma obligaría a convertir en cada importación.

    Ojo con la consecuencia: en 6/8 un compás son 3 negras, no 6. Lo que el
    metrónomo hace sonar seis veces son *pulsos* (corcheas), no beats. Los dos
    conceptos existen aquí a propósito y no son intercambiables.
*/
struct TimeSignature
{
    int numerator { 4 };
    int denominator { 4 };

    /** Duración del compás en negras. 4/4 → 4. 6/8 → 3. */
    constexpr double beatsPerBar() const noexcept
    {
        return static_cast<double> (numerator) * 4.0 / static_cast<double> (denominator);
    }

    /** Duración de un pulso de metrónomo en negras. 4/4 → 1. 6/8 → 0,5. */
    constexpr double beatsPerPulse() const noexcept
    {
        return 4.0 / static_cast<double> (denominator);
    }

    /** Pulsos de metrónomo por compás: el numerador, por definición. */
    constexpr int pulsesPerBar() const noexcept { return numerator; }

    constexpr bool isValid() const noexcept
    {
        return numerator > 0 && denominator > 0
            && (denominator & (denominator - 1)) == 0;   // 1, 2, 4, 8, 16...
    }

    friend constexpr bool operator== (TimeSignature a, TimeSignature b) noexcept
    {
        return a.numerator == b.numerator && a.denominator == b.denominator;
    }

    friend constexpr bool operator!= (TimeSignature a, TimeSignature b) noexcept
    {
        return ! (a == b);
    }
};

/** Posición musical legible. El compás y el pulso se cuentan desde 1, como los
    cuenta un músico; internamente todo son negras desde el pulso cero.
*/
struct MusicalPosition
{
    int bar { 1 };
    int pulse { 1 };
    double fractionOfPulse { 0.0 };   // [0, 1)
};

} // namespace keyla::core
