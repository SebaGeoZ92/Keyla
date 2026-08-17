#pragma once

// Las voces de cada instrumento. Todas cumplen el mismo contrato mínimo
// (start / startRelease / nextSample / currentLevel) y el reparto de voces, el
// pedal y el robo los pone PolyphonicInstrument.
//
// Ninguna asigna memoria ni toca nada global: todo el estado vive en la voz,
// que es un POD grande dentro del pool (invariante 1).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace keyla::core
{

inline constexpr double kTwoPi = 6.283185307179586476925286766559;

inline double midiToHertz (int note) noexcept
{
    return 440.0 * std::pow (2.0, (static_cast<double> (note) - 69.0) / 12.0);
}

/** Posición en el teclado, 0 en la nota más grave de un piano y 1 en la más
    aguda. Sirve para que cualquier parámetro pueda variar con el registro. */
inline double registerPosition (int pitch) noexcept
{
    return std::clamp ((pitch - 21) / 66.0, 0.0, 1.0);
}

inline double centsToRatio (double cents) noexcept
{
    return std::pow (2.0, cents / 1200.0);
}

// ── Envolvente ──────────────────────────────────────────────────────────────

/** ADSR sin la R separada del resto: ataque lineal, caída exponencial hacia un
    nivel de sostenido, y extinción exponencial al soltar.

    Un sostenido de 1 da un instrumento que suena mientras aguantas la tecla
    (órgano, acordeón, cuerdas). Un sostenido de 0 da uno que decae solo
    (piano, Rhodes, vibráfono). Es la diferencia más grande entre familias y
    aquí es un solo número.
*/
struct Envelope
{
    void start (double sampleRate, double attackSeconds, double decaySeconds,
                double sustain, double releaseSeconds) noexcept
    {
        value = 0.0;
        sustainLevel = sustain;
        releasing = false;

        attackInc = attackSeconds > 0.0 ? 1.0 / (attackSeconds * sampleRate) : 1.0;
        decayCoef = decaySeconds > 0.0 ? std::exp (-1.0 / (decaySeconds * sampleRate)) : 0.0;
        releaseCoef = releaseSeconds > 0.0 ? std::exp (-1.0 / (releaseSeconds * sampleRate)) : 0.0;
        attacking = true;
    }

    void startRelease() noexcept { releasing = true; }

    double next() noexcept
    {
        if (releasing)
        {
            value *= releaseCoef;
        }
        else if (attacking)
        {
            value += attackInc;

            if (value >= 1.0)
            {
                value = 1.0;
                attacking = false;
            }
        }
        else
        {
            value = sustainLevel + (value - sustainLevel) * decayCoef;
        }

        return value;
    }

    double level() const noexcept { return value; }

    double value { 0.0 };
    double sustainLevel { 1.0 };
    double attackInc { 1.0 };
    double decayCoef { 0.0 };
    double releaseCoef { 0.0 };
    bool attacking { true };
    bool releasing { false };
};

// ── Banco de parciales ──────────────────────────────────────────────────────

/** Suma de senoides con nivel propio. Es la base de casi todo lo de aquí.

    Se normaliza la **energía**, no la suma de niveles: con las fases repartidas
    los parciales no se suman en línea recta, y lo que el oído sigue es la
    energía. Sin esto, un timbre con veinte parciales sonaría al doble que uno
    con cinco — que fue exactamente el bug de los graves del piano.
*/
template <int Capacity>
struct PartialBank
{
    void clear() noexcept { count = 0; }

    /** Añade un parcial si cabe y si no se sale de lo audible. */
    void add (double frequency, double amplitude, double sampleRate, std::uint32_t& randomState) noexcept
    {
        if (count >= Capacity || frequency <= 0.0 || frequency >= sampleRate * 0.45)
            return;

        const auto index = static_cast<std::size_t> (count++);

        inc[index] = kTwoPi * frequency / sampleRate;
        level[index] = amplitude;

        // Fase repartida: si todas arrancan en cero, se suman alineadas el
        // primer instante y producen un pico que ninguna cuerda real tiene.
        randomState ^= randomState << 13;
        randomState ^= randomState >> 17;
        randomState ^= randomState << 5;
        phase[index] = (static_cast<double> (randomState) / 4294967296.0) * kTwoPi;
    }

    void normaliseEnergy() noexcept
    {
        double energy = 0.0;

        for (int i = 0; i < count; ++i)
            energy += level[static_cast<std::size_t> (i)] * level[static_cast<std::size_t> (i)];

        if (energy <= 0.0)
            return;

        const double norm = 1.0 / std::sqrt (energy);

        for (int i = 0; i < count; ++i)
            level[static_cast<std::size_t> (i)] *= norm;
    }

    double next() noexcept
    {
        double sum = 0.0;

        for (int i = 0; i < count; ++i)
        {
            const auto index = static_cast<std::size_t> (i);

            sum += std::sin (phase[index]) * level[index];
            phase[index] += inc[index];

            if (phase[index] >= kTwoPi)
                phase[index] -= kTwoPi;
        }

        return sum;
    }

    /** Multiplica la frecuencia de todos los parciales. Para vibrato y trémolo
        de afinación sin recalcular el banco. */
    void scaleFrequency (double ratio) noexcept
    {
        for (int i = 0; i < count; ++i)
            inc[static_cast<std::size_t> (i)] *= ratio;
    }

    int count { 0 };
    std::array<double, Capacity> phase {};
    std::array<double, Capacity> inc {};
    std::array<double, Capacity> level {};
};

} // namespace keyla::core
