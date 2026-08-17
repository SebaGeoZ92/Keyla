#pragma once

#include <cstdint>

namespace keyla::app
{

/** Lo único que el dominio de tiempo real le enseña a la UI (invariante 5).

    POD y copiable de un tirón: se publica con un seqlock y la UI hace polling a
    60 Hz. Jamás se notifica a la UI desde el callback de audio — mezclar esos
    dos hilos es la causa número uno de dropouts en aplicaciones de audio
    amateur (doc 01 §1.2).
*/
struct EngineSnapshot
{
    // ── Formato ─────────────────────────────────────────────────────────────
    double sampleRate { 0.0 };
    int bufferSize { 0 };
    double outputLatencyMs { 0.0 };

    // ── Reloj ───────────────────────────────────────────────────────────────
    std::uint64_t streamSamples { 0 };

    // ── Salud (doc 04 §7) ───────────────────────────────────────────────────
    double cpuMean { 0.0 };         // fracción del periodo de buffer
    double cpuPeak { 0.0 };
    std::uint64_t dropouts { 0 };
    std::uint64_t midiRejected { 0 };
    double callbackJitterMs { 0.0 };
    float gainReduction { 1.0f };
    float peakLevel { 0.0f };

    // ── Estado musical ──────────────────────────────────────────────────────
    int activeVoices { 0 };
    int numKeysDown { 0 };
    int sustainValue { 0 };
    std::uint64_t noteOnCount { 0 };

    /** Máscaras de 128 bits, una por nota MIDI. Dos `uint64` en vez de un
        array de bool: cabe en el snapshot sin coste y la UI lo lee de un
        vistazo. `sounding` incluye lo que retiene el pedal aunque la tecla esté
        suelta, y por eso son dos máscaras y no una. */
    std::uint64_t keysDown[2] { 0, 0 };
    std::uint64_t sounding[2] { 0, 0 };

    static bool hasBit (const std::uint64_t (&mask)[2], int note) noexcept
    {
        if (note < 0 || note > 127)
            return false;

        return (mask[note >> 6] & (std::uint64_t (1) << (note & 63))) != 0;
    }

    static void setBit (std::uint64_t (&mask)[2], int note) noexcept
    {
        if (note >= 0 && note <= 127)
            mask[note >> 6] |= (std::uint64_t (1) << (note & 63));
    }
};

} // namespace keyla::app
