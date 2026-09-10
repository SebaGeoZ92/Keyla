#pragma once

// Cuerda pulsada por guía de onda. Vive aparte de Voices.h porque no es una
// suma de senoides: es un modelo físico, y mezclarlo con los demás escondería
// que aquí se sintetiza de otra manera.

#include "Voices.h"

namespace keyla::core
{

/** Karplus-Strong: una cuerda pulsada de verdad, no imitada.

    La idea entera cabe en tres líneas. Un buffer circular con la longitud de
    un periodo de la nota se llena de ruido —el pinchazo de la púa o de la uña,
    que contiene todas las frecuencias— y luego se recircula pasándolo cada
    vuelta por un filtro de media. Ese filtro se lleva los agudos antes que los
    graves, que es exactamente lo que hace una cuerda: el brillo del ataque se
    apaga en un instante y queda el tono.

    Sale casi gratis —dos sumas y una multiplicación por sample, frente a los
    27 senos del acordeón— y suena a cuerda pulsada de una forma que la síntesis
    aditiva no consigue por mucho parcial que se le eche.

    Tres detalles que no son opcionales:

    - **El retardo tiene que ser fraccionario.** Con un retardo entero, un Do6
      cae 14 cents de su sitio. En una herramienta para aprender piano, un
      instrumento desafinado no es un defecto estético.
    - **La pérdida del bucle se calcula por nota.** Es el error que parece un
      detalle y no lo es: la ganancia se aplica una vez **por vuelta al buffer**,
      no una vez por sample, y el bucle da `f` vueltas por segundo. Con una
      ganancia fija, un Sol3 tardaba veinticinco segundos en callarse y la voz
      no se liberaba nunca. Encima de eso, el filtro de media pierde más cuanto
      más agudo, y de ahí que los agudos de una guitarra duren menos — eso sí es
      físico y se deja tal cual.
    - **Soltar la tecla no apaga la cuerda de golpe**, la amortigua. Es la mano
      posándose sobre ella.
*/

/** Carácter de la cuerda. Es un tipo y no unos campos para que el compilador
    vea las constantes: la voz se llama una vez por sample. */
struct GuitarCharacter
{
    static constexpr double decaySeconds = 1.60;    // constante de tiempo del bucle
    static constexpr double dampingSeconds = 0.16;  // al soltar: la mano apoyada
    static constexpr double brightnessFloor = 0.28; // púa blanda
    static constexpr double brightnessRange = 0.55; // ...y púa dura al pulsar fuerte
    static constexpr double velocitySensitivity = 1.0;
    static constexpr double amplitudeScale = 0.62;
};

/** El clavecín es la misma cuerda pulsada con otro carácter: plectro duro,
    cuerda corta y brillante, y —esto es lo que lo delata— **no responde a la
    fuerza**. El plectro pellizca igual la toques como la toques; los clavecines
    tienen dos teclados precisamente porque no se puede matizar con los dedos.
    Se deja una pizca de respuesta para que no se sienta muerto. */
struct HarpsichordCharacter
{
    static constexpr double decaySeconds = 0.85;
    static constexpr double dampingSeconds = 0.06;  // apagador de fieltro, rápido
    static constexpr double brightnessFloor = 0.80;
    static constexpr double brightnessRange = 0.08;
    static constexpr double velocitySensitivity = 0.12;
    static constexpr double amplitudeScale = 0.55;
};

template <typename Character>
struct PluckedStringVoice
{
    /** 4096 samples cubren hasta 11,7 Hz a 48 kHz y 23 Hz a 96 kHz: por debajo
        de la nota más grave de un piano en los dos casos.

        El tamaño importa por algo que no se ve: `PolyphonicInstrument` hace
        `slot = Slot{}` en cada Note On y en cada voz que se retira, y eso es un
        memset de este buffer **en el hilo de audio**. Con 4096 floats son 16 kB,
        del orden de 1,5 µs frente a los 3 ms de presupuesto del bloque. No
        viola el invariante 1 —no hay reserva de memoria, ni lock, ni I/O— pero
        crecer esto sin pensarlo sí acabaría costando un dropout. */
    static constexpr int capacity = 4096;

    void start (int pitch, int velocity, double sampleRate) noexcept
    {
        const double fundamental = midiToHertz (pitch);
        const double velocityNorm = std::clamp (velocity / 127.0, 0.0, 1.0);

        // El filtro de media añade medio sample de retardo al bucle: si no se
        // descuenta, la nota sale calada.
        delaySamples = std::clamp (sampleRate / fundamental - 0.5,
                                   2.0, static_cast<double> (capacity) - 3.0);

        writePos = 0;
        lastRead = 0.0;
        trackedLevel = 0.0;

        // Por vuelta al buffer, no por sample: el bucle da `fundamental`
        // vueltas por segundo, así que para que la nota dure `decaySeconds`
        // la pérdida tiene que repartirse entre esas vueltas y no entre los
        // 48 000 samples del segundo.
        loopGain = std::exp (-1.0 / (Character::decaySeconds * fundamental));

        // Pulsar fuerte no es sólo más volumen: es una púa que ataca más recta
        // y mete más agudo en la cuerda. Se modela filtrando el ruido inicial.
        const double brightness = Character::brightnessFloor
                                + Character::brightnessRange * velocityNorm * velocityNorm;

        std::uint32_t random = static_cast<std::uint32_t> (pitch) * 2891336453u
                             + static_cast<std::uint32_t> (velocity) * 374761393u + 13u;

        const int noiseLength = static_cast<int> (std::ceil (delaySamples)) + 2;
        double filtered = 0.0;

        for (int i = 0; i < noiseLength; ++i)
        {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;

            const double white = static_cast<double> (random) / 2147483648.0 - 1.0;
            filtered += (white - filtered) * brightness;

            buffer[static_cast<std::size_t> (capacity - noiseLength + i)]
                = static_cast<float> (filtered);
        }

        // Sostenido 1: la cuerda no se apaga sola por envolvente, se apaga
        // porque el bucle pierde energía. La envolvente sólo existe para el
        // apagador al soltar la tecla.
        envelope.start (sampleRate, 0.0005, 0.0, 1.0, Character::dampingSeconds);

        const double response = 1.0 - Character::velocitySensitivity
                              + Character::velocitySensitivity * std::pow (velocityNorm, 1.2);

        amplitude = Character::amplitudeScale * response;
    }

    void startRelease (double) noexcept { envelope.startRelease(); }

    double nextSample() noexcept
    {
        double readPos = static_cast<double> (writePos) - delaySamples;

        if (readPos < 0.0)
            readPos += capacity;

        const auto i0 = static_cast<int> (readPos);
        const double frac = readPos - i0;
        const int i1 = i0 + 1 >= capacity ? 0 : i0 + 1;

        const double a = buffer[static_cast<std::size_t> (i0)];
        const double b = buffer[static_cast<std::size_t> (i1)];
        const double sample = a + frac * (b - a);

        // El filtro de la cuerda: media de dos samples consecutivos. Es un paso
        // bajo cuya pérdida crece con la frecuencia, y de ahí sale que los
        // agudos de una guitarra duren menos que los graves.
        buffer[static_cast<std::size_t> (writePos)]
            = static_cast<float> ((sample + lastRead) * 0.5 * loopGain);

        lastRead = sample;

        if (++writePos >= capacity)
            writePos = 0;

        const double magnitude = std::abs (sample);

        if (magnitude > trackedLevel)
            trackedLevel = magnitude;
        else
            trackedLevel *= levelFollowDecay;

        return sample * envelope.next() * amplitude;
    }

    /** La cuerda no tiene envolvente que consultar: se sigue su nivel real.
        Sin esto la voz no se retiraría nunca y el pool se agotaría en cuatro
        compases. */
    double currentLevel() const noexcept { return trackedLevel * envelope.level() * amplitude; }

    static constexpr double levelFollowDecay = 0.99995;

    std::array<float, capacity> buffer {};
    double delaySamples { 100.0 };
    double loopGain { 0.999 };
    double lastRead { 0.0 };
    double trackedLevel { 0.0 };
    double amplitude { 0.0 };
    int writePos { 0 };
    Envelope envelope;
};

using GuitarVoice = PluckedStringVoice<GuitarCharacter>;
using HarpsichordVoice = PluckedStringVoice<HarpsichordCharacter>;

} // namespace keyla::core
