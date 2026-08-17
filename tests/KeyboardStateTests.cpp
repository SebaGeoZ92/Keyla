// "Tecla suelta" y "nota apagada" no son lo mismo en cuanto hay pedal, y
// confundirlas rompe la visualización y la evaluación del legato (doc 02 §4).

#include <core/midi/KeyboardState.h>

#include <catch2/catch_test_macros.hpp>

using namespace keyla::core;

namespace
{
    RawMidiMessage noteOn (int pitch, int velocity, int channel = 1)
    {
        RawMidiMessage m;
        m.bytes[0] = static_cast<std::uint8_t> (0x90 | (channel - 1));
        m.bytes[1] = static_cast<std::uint8_t> (pitch);
        m.bytes[2] = static_cast<std::uint8_t> (velocity);
        m.size = 3;
        return m;
    }

    RawMidiMessage noteOff (int pitch, int channel = 1)
    {
        RawMidiMessage m;
        m.bytes[0] = static_cast<std::uint8_t> (0x80 | (channel - 1));
        m.bytes[1] = static_cast<std::uint8_t> (pitch);
        m.bytes[2] = 0;
        m.size = 3;
        return m;
    }

    RawMidiMessage controller (int number, int value, int channel = 1)
    {
        RawMidiMessage m;
        m.bytes[0] = static_cast<std::uint8_t> (0xB0 | (channel - 1));
        m.bytes[1] = static_cast<std::uint8_t> (number);
        m.bytes[2] = static_cast<std::uint8_t> (value);
        m.size = 3;
        return m;
    }
}

TEST_CASE ("Note On con velocity 0 es un Note Off", "[midi]")
{
    // La mitad de los teclados lo mandan así, y el estándar lo permite.
    const auto message = noteOn (60, 0);

    CHECK (! message.isNoteOn());
    CHECK (message.isNoteOff());
}

TEST_CASE ("Los accesores del mensaje crudo interpretan bien", "[midi]")
{
    const auto on = noteOn (64, 100, 3);
    CHECK (on.isNoteOn());
    CHECK (! on.isNoteOff());
    CHECK (on.noteNumber() == 64);
    CHECK (on.velocity() == 100);
    CHECK (on.channel() == 3);

    const auto pedal = controller (64, 127);
    CHECK (pedal.isController());
    CHECK (pedal.isSustainPedal());
    CHECK (pedal.controllerValue() == 127);

    const auto modWheel = controller (1, 64);
    CHECK (modWheel.isController());
    CHECK (! modWheel.isSustainPedal());
}

TEST_CASE ("Una tecla pulsada y soltada, sin pedal", "[midi][keyboard]")
{
    KeyboardState keyboard;

    keyboard.apply (noteOn (60, 90), 1000);

    CHECK (keyboard.isKeyDown (60));
    CHECK (keyboard.isSounding (60));
    CHECK (! keyboard.isHeldByPedalOnly (60));
    CHECK (keyboard.velocityOf (60) == 90);
    CHECK (keyboard.pressedAtSample (60) == 1000);
    CHECK (keyboard.numKeysDown() == 1);

    keyboard.apply (noteOff (60), 2000);

    CHECK (! keyboard.isKeyDown (60));
    CHECK (! keyboard.isSounding (60));
    CHECK (keyboard.numKeysDown() == 0);
}

TEST_CASE ("Con el pedal pisado, soltar la tecla no apaga la nota", "[midi][keyboard][pedal]")
{
    KeyboardState keyboard;

    keyboard.apply (controller (64, 127), 0);
    REQUIRE (keyboard.isSustainDown());

    keyboard.apply (noteOn (60, 80), 1000);
    keyboard.apply (noteOff (60), 2000);

    // La tecla está suelta pero la nota sigue sonando. Ésa es la distinción.
    CHECK (! keyboard.isKeyDown (60));
    CHECK (keyboard.isSounding (60));
    CHECK (keyboard.isHeldByPedalOnly (60));
    CHECK (keyboard.numKeysDown() == 0);
    CHECK (keyboard.numSounding() == 1);

    keyboard.apply (controller (64, 0), 3000);

    CHECK (! keyboard.isSounding (60));
    CHECK (keyboard.numSounding() == 0);
}

TEST_CASE ("Levantar el pedal no apaga lo que sigue pulsado", "[midi][keyboard][pedal]")
{
    KeyboardState keyboard;

    keyboard.apply (noteOn (60, 80), 0);      // ésta se queda pulsada
    keyboard.apply (noteOn (64, 80), 0);      // ésta se soltará
    keyboard.apply (controller (64, 127), 10);
    keyboard.apply (noteOff (64), 20);

    REQUIRE (keyboard.isSounding (60));
    REQUIRE (keyboard.isHeldByPedalOnly (64));

    keyboard.apply (controller (64, 0), 30);

    CHECK (keyboard.isKeyDown (60));
    CHECK (keyboard.isSounding (60));         // sigue: la tecla no se ha soltado
    CHECK (! keyboard.isSounding (64));
}

TEST_CASE ("Volver a pulsar una nota retenida la reengancha a la tecla", "[midi][keyboard][pedal]")
{
    KeyboardState keyboard;

    keyboard.apply (controller (64, 127), 0);
    keyboard.apply (noteOn (60, 80), 10);
    keyboard.apply (noteOff (60), 20);
    REQUIRE (keyboard.isHeldByPedalOnly (60));

    keyboard.apply (noteOn (60, 110), 30);

    CHECK (keyboard.isKeyDown (60));
    CHECK (! keyboard.isHeldByPedalOnly (60));
    CHECK (keyboard.velocityOf (60) == 110);
    CHECK (keyboard.pressedAtSample (60) == 30);
    CHECK (keyboard.numKeysDown() == 1);

    // Y al levantar el pedal la nota no se va, porque la tecla está pulsada.
    keyboard.apply (controller (64, 0), 40);
    CHECK (keyboard.isSounding (60));
}

TEST_CASE ("El medio pedal se guarda entero aunque sólo se use el umbral", "[midi][keyboard][pedal]")
{
    KeyboardState keyboard;

    keyboard.apply (controller (64, 63), 0);
    CHECK (! keyboard.isSustainDown());
    CHECK (keyboard.sustainPedalValue() == 63);

    keyboard.apply (controller (64, 64), 0);
    CHECK (keyboard.isSustainDown());
    CHECK (keyboard.sustainPedalValue() == 64);
}

TEST_CASE ("Dos Note On seguidos de la misma tecla no descuadran la cuenta", "[midi][keyboard]")
{
    KeyboardState keyboard;

    keyboard.apply (noteOn (60, 80), 0);
    keyboard.apply (noteOn (60, 90), 10);     // sin Note Off en medio

    CHECK (keyboard.numKeysDown() == 1);

    keyboard.apply (noteOff (60), 20);
    CHECK (keyboard.numKeysDown() == 0);

    // Y un Note Off de una tecla que no estaba pulsada tampoco.
    keyboard.apply (noteOff (60), 30);
    CHECK (keyboard.numKeysDown() == 0);
}

TEST_CASE ("El rango de lo pulsado sirve para conocer el teclado", "[midi][keyboard]")
{
    KeyboardState keyboard;

    CHECK (keyboard.lowestKeyDown() == -1);
    CHECK (keyboard.highestKeyDown() == -1);

    keyboard.apply (noteOn (48, 80), 0);
    keyboard.apply (noteOn (72, 80), 0);
    keyboard.apply (noteOn (60, 80), 0);

    CHECK (keyboard.lowestKeyDown() == 48);
    CHECK (keyboard.highestKeyDown() == 72);
    CHECK (keyboard.numKeysDown() == 3);
}

TEST_CASE ("Alturas fuera de rango no se llevan nada por delante", "[midi][keyboard]")
{
    KeyboardState keyboard;

    keyboard.noteOn (-1, 100, 0);
    keyboard.noteOn (128, 100, 0);
    keyboard.noteOn (999, 100, 0);

    CHECK (keyboard.numKeysDown() == 0);
    CHECK (! keyboard.isKeyDown (-1));
    CHECK (! keyboard.isSounding (128));
}

TEST_CASE ("reset() deja el teclado como recién abierto", "[midi][keyboard]")
{
    KeyboardState keyboard;

    keyboard.apply (controller (64, 127), 0);
    keyboard.apply (noteOn (60, 80), 0);
    keyboard.apply (noteOn (64, 80), 0);

    keyboard.reset();

    CHECK (keyboard.numKeysDown() == 0);
    CHECK (keyboard.numSounding() == 0);
    CHECK (! keyboard.isSustainDown());
    CHECK (keyboard.sustainPedalValue() == 0);
}
