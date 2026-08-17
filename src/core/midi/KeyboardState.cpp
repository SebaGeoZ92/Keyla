#include "KeyboardState.h"

namespace keyla::core
{

void KeyboardState::reset() noexcept
{
    notes.fill (NoteState {});
    sustainValue = 0;
    keysDown = 0;
}

void KeyboardState::apply (const RawMidiMessage& message, std::uint64_t sample) noexcept
{
    // El orden importa: isNoteOff() ya cubre el Note On con velocity 0, así que
    // se pregunta primero por Note On, que es el caso más estricto.
    if (message.isNoteOn())
        noteOn (message.noteNumber(), message.velocity(), sample);
    else if (message.isNoteOff())
        noteOff (message.noteNumber(), sample);
    else if (message.isSustainPedal())
        setSustainPedal (message.controllerValue(), sample);
}

void KeyboardState::noteOn (int pitch, int velocity, std::uint64_t sample) noexcept
{
    if (! isValidPitch (pitch))
        return;

    auto& note = notes[static_cast<std::size_t> (pitch)];

    if (! note.keyDown)
        ++keysDown;

    note.keyDown = true;
    note.pedalHeld = false;         // volver a pulsarla la reengancha a la tecla
    note.velocity = static_cast<std::uint8_t> (velocity);
    note.pressedAt = sample;
}

void KeyboardState::noteOff (int pitch, std::uint64_t /*sample*/) noexcept
{
    if (! isValidPitch (pitch))
        return;

    auto& note = notes[static_cast<std::size_t> (pitch)];

    if (! note.keyDown)
        return;

    note.keyDown = false;
    --keysDown;

    // Soltar la tecla con el pedal pisado no apaga la nota: la traspasa al
    // pedal. Es justo lo que distingue "tecla suelta" de "nota apagada".
    if (isSustainDown())
        note.pedalHeld = true;
}

void KeyboardState::setSustainPedal (int value, std::uint64_t /*sample*/) noexcept
{
    const bool wasDown = isSustainDown();
    sustainValue = value < 0 ? 0 : (value > 127 ? 127 : value);

    if (wasDown && ! isSustainDown())
    {
        // Al levantar el pedal se apaga todo lo que sólo él sostenía. Lo que
        // sigue con la tecla pulsada no se toca.
        for (auto& note : notes)
            note.pedalHeld = false;
    }
}

bool KeyboardState::isKeyDown (int pitch) const noexcept
{
    return isValidPitch (pitch) && notes[static_cast<std::size_t> (pitch)].keyDown;
}

bool KeyboardState::isSounding (int pitch) const noexcept
{
    if (! isValidPitch (pitch))
        return false;

    const auto& note = notes[static_cast<std::size_t> (pitch)];
    return note.keyDown || note.pedalHeld;
}

bool KeyboardState::isHeldByPedalOnly (int pitch) const noexcept
{
    if (! isValidPitch (pitch))
        return false;

    const auto& note = notes[static_cast<std::size_t> (pitch)];
    return note.pedalHeld && ! note.keyDown;
}

int KeyboardState::velocityOf (int pitch) const noexcept
{
    return isValidPitch (pitch) ? notes[static_cast<std::size_t> (pitch)].velocity : 0;
}

std::uint64_t KeyboardState::pressedAtSample (int pitch) const noexcept
{
    return isValidPitch (pitch) ? notes[static_cast<std::size_t> (pitch)].pressedAt : 0;
}

int KeyboardState::numSounding() const noexcept
{
    int count = 0;

    for (const auto& note : notes)
        if (note.keyDown || note.pedalHeld)
            ++count;

    return count;
}

int KeyboardState::lowestKeyDown() const noexcept
{
    for (int pitch = 0; pitch < numNotes; ++pitch)
        if (notes[static_cast<std::size_t> (pitch)].keyDown)
            return pitch;

    return -1;
}

int KeyboardState::highestKeyDown() const noexcept
{
    for (int pitch = numNotes - 1; pitch >= 0; --pitch)
        if (notes[static_cast<std::size_t> (pitch)].keyDown)
            return pitch;

    return -1;
}

} // namespace keyla::core
