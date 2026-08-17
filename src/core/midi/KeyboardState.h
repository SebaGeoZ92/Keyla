#pragma once

#include "MidiEvent.h"

#include <array>
#include <cstdint>

namespace keyla::core
{

/** Qué está pulsado, desde cuándo, con qué fuerza — y qué sigue sonando por
    el pedal aunque la tecla esté suelta (doc 02 §4).

    Esa última distinción parece un detalle y no lo es: *tecla suelta* y *nota
    apagada* son cosas distintas en cuanto hay pedal, y confundirlas rompe a la
    vez la visualización (el teclado virtual pintaría teclas que no están
    pulsadas) y la evaluación de legato y de uso del pedal.

    Vive en el dominio de sesión. No es thread-safe: se alimenta de los eventos
    que llegan por FIFO desde el hilo de audio.
*/
class KeyboardState
{
public:
    static constexpr int numNotes = 128;

    void reset() noexcept;

    /** Alimenta el estado con un mensaje ya sellado. Ignora lo que no le
        incumbe, así que se le puede pasar el flujo entero. */
    void apply (const RawMidiMessage& message, std::uint64_t sample) noexcept;

    void noteOn (int pitch, int velocity, std::uint64_t sample) noexcept;
    void noteOff (int pitch, std::uint64_t sample) noexcept;

    /** Valor continuo de CC64, 0..127. Se guarda entero para el medio pedal,
        aunque de momento sólo se use contra el umbral. */
    void setSustainPedal (int value, std::uint64_t sample) noexcept;

    // ── Consulta ────────────────────────────────────────────────────────────

    bool isKeyDown (int pitch) const noexcept;

    /** Suena: o la tecla está pulsada, o el pedal la está reteniendo. */
    bool isSounding (int pitch) const noexcept;

    /** Suelta pero retenida por el pedal. Para pintarla distinto. */
    bool isHeldByPedalOnly (int pitch) const noexcept;

    int velocityOf (int pitch) const noexcept;
    std::uint64_t pressedAtSample (int pitch) const noexcept;

    bool isSustainDown() const noexcept { return sustainValue >= sustainPedalThreshold; }
    int sustainPedalValue() const noexcept { return sustainValue; }

    int numKeysDown() const noexcept { return keysDown; }
    int numSounding() const noexcept;

    /** -1 si no hay ninguna. */
    int lowestKeyDown() const noexcept;
    int highestKeyDown() const noexcept;

private:
    struct NoteState
    {
        bool keyDown { false };
        bool pedalHeld { false };
        std::uint8_t velocity { 0 };
        std::uint64_t pressedAt { 0 };
    };

    static bool isValidPitch (int pitch) noexcept { return pitch >= 0 && pitch < numNotes; }

    std::array<NoteState, numNotes> notes {};
    int sustainValue { 0 };
    int keysDown { 0 };
};

} // namespace keyla::core
