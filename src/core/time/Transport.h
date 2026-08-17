#pragma once

#include "TimeSignature.h"

#include <cstdint>

namespace keyla::core
{

/** El reloj maestro del proyecto (doc 02 §3, invariante 2).

    Todo evento musical —una nota que tocas, un clic de metrónomo, una nota
    esperada de un ejercicio— se expresa como posición en el contador de samples
    del stream de audio. Nunca en `std::chrono` ni en el reloj del sistema: el
    reloj de la tarjeta deriva decenas de ppm respecto al del sistema, y en una
    sesión de diez minutos eso son varios milisegundos de error que no son del
    alumno sino del hardware (doc 01 §1.3).

    Consecuencia de tenerlo así: el pulso N está exactamente en
    `barZeroSample + N * (60/bpm) * sampleRate`. El metrónomo no deriva nunca,
    porque no se reprograma — se calcula.

    ### Hilos

    Vive en el hilo de audio, que es el único que llama a `advance()`. Los
    cambios de tempo o de compás llegan por FIFO desde el dominio de sesión y
    se aplican al principio de un bloque. No es thread-safe a propósito: para
    leerlo desde la UI se publica un snapshot (invariante 5).

    ### Precisión

    Las conversiones toman y devuelven `double` en vez de enteros para no
    perder la posición fraccionaria (invariante 3). Cuantizar es cosa de quien
    sintetiza, no de quien mide.
*/
class Transport
{
public:
    Transport() = default;

    // ── Configuración (fuera del hilo de audio, con el motor parado) ────────

    void prepare (double sampleRateToUse) noexcept;

    // ── Reloj ───────────────────────────────────────────────────────────────

    /** Avanza el contador. Lo llama el callback de audio al terminar el bloque. */
    void advance (int numSamples) noexcept;

    void resetToZero() noexcept;

    std::uint64_t samplePosition() const noexcept { return streamSample; }
    double sampleRate() const noexcept { return rate; }
    double seconds() const noexcept { return static_cast<double> (streamSample) / rate; }

    // ── Tempo y compás ──────────────────────────────────────────────────────

    /** Negras por minuto. Ver el convenio de pulso en TimeSignature. */
    void setTempo (double beatsPerMinute) noexcept;
    double tempo() const noexcept { return bpm; }

    void setTimeSignature (TimeSignature signature) noexcept;
    TimeSignature timeSignature() const noexcept { return meter; }

    /** Dónde cae el pulso cero: el origen de la rejilla musical. */
    void setBarZeroSample (double sample) noexcept { barZero = sample; }
    double barZeroSample() const noexcept { return barZero; }

    /** Duración de una negra en samples. */
    double samplesPerBeat() const noexcept { return 60.0 / bpm * rate; }

    // ── Conversiones ────────────────────────────────────────────────────────
    //
    // Fraccionarias en los dos sentidos. Un sample anterior al pulso cero da un
    // beat negativo, que es correcto y a veces necesario (una anacrusa).

    double sampleToBeat (double samplePos) const noexcept;
    double beatToSample (double beat) const noexcept;

    double currentBeat() const noexcept { return sampleToBeat (static_cast<double> (streamSample)); }

    // ── Rejilla musical ─────────────────────────────────────────────────────

    MusicalPosition beatToPosition (double beat) const noexcept;
    double positionToBeat (MusicalPosition position) const noexcept;

    /** Índice del pulso de metrónomo, contando desde el pulso cero. Puede ser
        negativo antes del origen. */
    double beatToPulse (double beat) const noexcept { return beat / meter.beatsPerPulse(); }
    double pulseToBeat (double pulse) const noexcept { return pulse * meter.beatsPerPulse(); }

    /** Sample exacto del pulso `pulseIndex`. Esto es lo que consume el
        metrónomo: no hay acumulación de error porque no hay acumulación. */
    double pulseToSample (double pulseIndex) const noexcept { return beatToSample (pulseToBeat (pulseIndex)); }

    /** True si ese pulso cae en el primer tiempo de un compás — el acento. */
    bool isDownbeat (std::int64_t pulseIndex) const noexcept;

private:
    double rate { 48000.0 };
    double bpm { 100.0 };
    double barZero { 0.0 };
    TimeSignature meter {};
    std::uint64_t streamSample { 0 };
};

} // namespace keyla::core
