#pragma once

#include <cstdint>

namespace keyla::core
{

/** Un mensaje MIDI corto tal y como sale del driver, sin interpretar.

    Se guarda crudo a propósito (doc 02 §4): grabar crudo es gratis y evita
    tener que repetir sesiones cuando dentro de un año quieras analizar algo en
    lo que hoy no has pensado. La interpretación vive en los accesores.

    Sysex no cabe aquí. En el dominio de tiempo real no se quiere memoria
    variable, y para lo que hace Keyla no aporta nada.
*/
struct RawMidiMessage
{
    std::uint8_t bytes[3] { 0, 0, 0 };
    std::uint8_t size { 0 };

    std::uint8_t status()  const noexcept { return static_cast<std::uint8_t> (bytes[0] & 0xF0u); }
    int channel()          const noexcept { return (bytes[0] & 0x0Fu) + 1; }   // 1..16
    int data1()            const noexcept { return size > 1 ? bytes[1] : 0; }
    int data2()            const noexcept { return size > 2 ? bytes[2] : 0; }

    /** Note On con velocity 0 se normaliza a Note Off, como manda el estándar
        y como lo mandan la mitad de los teclados. */
    bool isNoteOn()  const noexcept { return status() == 0x90 && data2() > 0; }
    bool isNoteOff() const noexcept { return status() == 0x80 || (status() == 0x90 && data2() == 0); }

    int noteNumber() const noexcept { return data1(); }
    int velocity()   const noexcept { return data2(); }

    bool isController() const noexcept { return status() == 0xB0; }
    int controllerNumber() const noexcept { return data1(); }
    int controllerValue()  const noexcept { return data2(); }

    /** CC64. El valor continuo se conserva para el medio pedal, aunque de
        momento sólo se use el umbral (doc 02 §4). */
    bool isSustainPedal() const noexcept { return isController() && controllerNumber() == 64; }

    bool isPitchBend() const noexcept { return status() == 0xE0; }
    int pitchBendValue() const noexcept { return (data2() << 7) | data1(); }   // 0..16383
};

/** El umbral por el que un pedal continuo cuenta como pisado. */
inline constexpr int sustainPedalThreshold = 64;

/** Un mensaje ya sellado en el dominio de samples del stream (doc 02 §3).

    Las dos posiciones existen a la vez y no son redundantes — es la distinción
    del invariante 3:

    - `exactSample` conserva la posición fraccionaria real. Es lo que consume la
      evaluación, y por eso la precisión de medida no está limitada por el
      tamaño del buffer: con buffers de 3 ms se mide timing muy por debajo del
      milisegundo.
    - `renderOffset` es el sample dentro del bloque actual en el que hay que
      hacerlo sonar. Ya ocurrió, así que se cuantiza al bloque: la granularidad
      del buffer es un límite físico de la síntesis, no de la medición.
*/
struct StampedMidiEvent
{
    RawMidiMessage message;
    double exactSample { 0.0 };
    int renderOffset { 0 };
};

} // namespace keyla::core
