#pragma once

#include "../midi/MidiEvent.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstddef>
#include <string>

namespace keyla::core
{

/** Una ventana sobre los eventos del bloque actual. No es dueña de nada: el
    hilo de audio no asigna memoria (invariante 1). */
struct MidiEventSpan
{
    const StampedMidiEvent* data { nullptr };
    std::size_t count { 0 };

    const StampedMidiEvent* begin() const noexcept { return data; }
    const StampedMidiEvent* end()   const noexcept { return data + count; }
    bool empty() const noexcept { return count == 0; }
};

struct InstrumentInfo
{
    std::string name;
    int maxPolyphony { 0 };
};

/** Contrato de cualquier fuente de sonido (doc 02 §2).

    Existe para que el motor de aprendizaje **nunca** conozca el instrumento.
    Cambiar el sampler de piano por un órgano, o meter sfizz en la fase 8, no
    debe tocar una línea del motor de ejercicios.
*/
class IInstrument
{
public:
    virtual ~IInstrument() = default;

    /** Fuera del hilo de audio, con el motor parado. Aquí es donde se asigna
        toda la memoria que el instrumento vaya a necesitar después. */
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void release() = 0;

    /** **En el hilo de audio.** Sin asignar, sin locks, sin I/O, sin logs y sin
        excepciones (invariante 1). Los eventos vienen con su offset de sample
        dentro del bloque, ya cuantizado; el instrumento no tiene por qué saber
        nada de la posición fraccionaria, que es cosa de la evaluación.

        Suma sobre `output`, no lo sobrescribe: hay más de una fuente en el
        grafo (doc 02 §2). */
    virtual void process (juce::AudioBuffer<float>& output, const MidiEventSpan& events) = 0;

    /** Corta todo sonido inmediatamente. Para cambios de dispositivo y pánico. */
    virtual void reset() = 0;

    virtual InstrumentInfo info() const = 0;

    /** Voces sonando ahora mismo, para el panel de salud. -1 si el instrumento
        no lleva la cuenta. Informativo: nadie debe tomar decisiones con esto. */
    virtual int activeVoiceCount() const noexcept { return -1; }
};

} // namespace keyla::core
