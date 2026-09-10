#pragma once

// Los instrumentos de Keyla. Cada uno es una voz y una línea de typedef: toda
// la maquinaria de reparto, pedal y robo la pone PolyphonicInstrument.

#include "PluckedString.h"
#include "PolyphonicInstrument.h"
#include "Voices.h"

#include <juce_core/juce_core.h>

#include <memory>
#include <vector>

namespace keyla::core
{

// ── Órgano ──────────────────────────────────────────────────────────────────

/** Órgano de ruedas fónicas. Suena mientras aguantas la tecla y se corta casi
    en seco al soltar: no hay cuerda que siga vibrando.

    Tres cosas lo delatan como órgano y no como suma de senos:

    - **Los tiradores.** Las alturas no son la serie armónica entera sino un
      juego fijo con sub-octava y quinta (16', 5⅓', 8', 4', 2⅔', 2', 1⅗', 1').
      Esa quinta grave es la mitad del carácter.
    - **El clic de tecla.** El contacto es mecánico y suena. Sin él, un órgano
      sintetizado se oye "de plástico".
    - **No responde a la velocity.** Es fiel al instrumento, pero se deja un
      poco de respuesta para que no se sienta muerto en un teclado moderno.
*/
struct OrganVoice
{
    void start (int pitch, int velocity, double sampleRate) noexcept
    {
        const double fundamental = midiToHertz (pitch);
        const double velocityNorm = std::clamp (velocity / 127.0, 0.0, 1.0);

        // Tiradores: proporción de frecuencia y nivel. Registro clásico.
        static constexpr double ratios[] = { 0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 5.0, 8.0 };
        static constexpr double levels[] = { 0.55, 1.0, 0.35, 0.65, 0.25, 0.40, 0.15, 0.20 };

        std::uint32_t random = static_cast<std::uint32_t> (pitch) * 2246822519u + 7u;

        bank.clear();

        for (int i = 0; i < 8; ++i)
            bank.add (fundamental * ratios[i], levels[i], sampleRate, random);

        bank.normaliseEnergy();

        envelope.start (sampleRate, 0.006, 0.0, 1.0, 0.045);
        amplitude = 0.30 * (0.65 + 0.35 * velocityNorm);

        clickLevel = 0.35 * velocityNorm;
        clickDecay = std::exp (-1.0 / (0.0035 * sampleRate));
        noiseState = random | 1u;

        vibratoPhase = 0.0;
        vibratoInc = kTwoPi * 6.2 / sampleRate;
    }

    void startRelease (double) noexcept { envelope.startRelease(); }

    double nextSample() noexcept
    {
        // Vibrato suave: en un órgano real lo produce una línea de retardo, y
        // una modulación pequeña de afinación se le parece bastante.
        vibratoPhase += vibratoInc;

        if (vibratoPhase >= kTwoPi)
            vibratoPhase -= kTwoPi;

        double sample = bank.next();

        if (clickLevel > 1.0e-5)
        {
            noiseState ^= noiseState << 13;
            noiseState ^= noiseState >> 17;
            noiseState ^= noiseState << 5;

            sample += (static_cast<double> (noiseState) / 2147483648.0 - 1.0) * clickLevel;
            clickLevel *= clickDecay;
        }

        const double tremolo = 1.0 + 0.035 * std::sin (vibratoPhase);
        return sample * envelope.next() * amplitude * tremolo;
    }

    double currentLevel() const noexcept { return envelope.level() * amplitude + clickLevel; }

    PartialBank<8> bank;
    Envelope envelope;
    double amplitude { 0.0 };
    double clickLevel { 0.0 };
    double clickDecay { 0.0 };
    double vibratoPhase { 0.0 };
    double vibratoInc { 0.0 };
    std::uint32_t noiseState { 1 };
};

// ── Piano eléctrico ─────────────────────────────────────────────────────────

/** Piano eléctrico de púa. Síntesis FM de dos operadores, que es como se hizo
    históricamente y sigue siendo lo más convincente por lo que cuesta.

    El truco está en que **el índice de modulación decae mucho más rápido que
    la nota**: eso produce el "ladrido" metálico del ataque que se apaga en
    seguida y deja un tono casi puro. Y ese índice sube con la fuerza — tocar
    fuerte no da lo mismo más alto, da más mordiente. Sin eso no suena a Rhodes,
    suena a campana.
*/
struct ElectricPianoVoice
{
    void start (int pitch, int velocity, double sampleRate) noexcept
    {
        const double fundamental = midiToHertz (pitch);
        const double velocityNorm = std::clamp (velocity / 127.0, 0.0, 1.0);
        const double t = registerPosition (pitch);

        carrierInc = kTwoPi * fundamental / sampleRate;
        modulatorInc = carrierInc;              // relación 1:1
        carrierPhase = 0.0;
        modulatorPhase = 0.0;

        // Mordiente: mucho al golpear fuerte, casi nada al acariciar.
        modulationIndex = 0.4 + 6.5 * velocityNorm * velocityNorm;
        modulationDecay = std::exp (-1.0 / (0.055 * sampleRate));

        // La púa: un armónico alto que sólo dura el ataque.
        tineInc = kTwoPi * fundamental * 7.0 / sampleRate;
        tinePhase = 0.0;
        tineLevel = 0.22 * velocityNorm * velocityNorm;
        tineDecay = std::exp (-1.0 / (0.030 * sampleRate));

        const double decaySeconds = 4.5 * std::pow (0.30, t);
        envelope.start (sampleRate, 0.002, decaySeconds, 0.0, 0.30);

        amplitude = 0.42 * std::pow (velocityNorm, 1.3);
    }

    void startRelease (double) noexcept { envelope.startRelease(); }

    double nextSample() noexcept
    {
        modulatorPhase += modulatorInc;

        if (modulatorPhase >= kTwoPi)
            modulatorPhase -= kTwoPi;

        const double modulator = std::sin (modulatorPhase) * modulationIndex;

        carrierPhase += carrierInc;

        if (carrierPhase >= kTwoPi)
            carrierPhase -= kTwoPi;

        double sample = std::sin (carrierPhase + modulator);

        if (tineLevel > 1.0e-5)
        {
            tinePhase += tineInc;

            if (tinePhase >= kTwoPi)
                tinePhase -= kTwoPi;

            sample += std::sin (tinePhase) * tineLevel;
            tineLevel *= tineDecay;
        }

        modulationIndex *= modulationDecay;

        return sample * envelope.next() * amplitude;
    }

    double currentLevel() const noexcept { return envelope.level() * amplitude; }

    double carrierPhase { 0.0 }, carrierInc { 0.0 };
    double modulatorPhase { 0.0 }, modulatorInc { 0.0 };
    double modulationIndex { 0.0 }, modulationDecay { 0.0 };
    double tinePhase { 0.0 }, tineInc { 0.0 }, tineLevel { 0.0 }, tineDecay { 0.0 };
    Envelope envelope;
    double amplitude { 0.0 };
};

// ── Acordeón ────────────────────────────────────────────────────────────────

/** Acordeón con afinación de musette.

    Lo que hace que un acordeón suene a acordeón y no a órgano es que **cada
    nota suena por dos o tres lengüetas ligeramente desafinadas entre sí**. Ese
    batido lento es el sonido; afinándolas iguales se pierde el instrumento.
    Aquí van tres lengüetas a ±11 cents, que es una musette de las de cumbia.

    El espectro es de lengüeta libre: rico y con caída lenta, más cerca de un
    diente de sierra que de una senoide.
*/
struct AccordionVoice
{
    void start (int pitch, int velocity, double sampleRate) noexcept
    {
        const double fundamental = midiToHertz (pitch);
        const double velocityNorm = std::clamp (velocity / 127.0, 0.0, 1.0);

        std::uint32_t random = static_cast<std::uint32_t> (pitch) * 3266489917u + 11u;

        bank.clear();

        // Tres lengüetas: una afinada y dos batiendo contra ella.
        static constexpr double detuneCents[] = { -11.0, 0.0, 11.0 };

        // El brillo sube con la fuerza porque el fuelle empuja más aire.
        const double rolloff = 1.35 - 0.35 * velocityNorm;

        for (double cents : detuneCents)
        {
            const double reedFrequency = fundamental * centsToRatio (cents);

            for (int harmonic = 1; harmonic <= 9; ++harmonic)
                bank.add (reedFrequency * harmonic,
                          std::pow (1.0 / harmonic, rolloff),
                          sampleRate, random);
        }

        bank.normaliseEnergy();

        envelope.start (sampleRate, 0.030, 0.0, 1.0, 0.070);
        amplitude = 0.34 * (0.55 + 0.45 * velocityNorm);
    }

    void startRelease (double) noexcept { envelope.startRelease(); }

    double nextSample() noexcept { return bank.next() * envelope.next() * amplitude; }

    double currentLevel() const noexcept { return envelope.level() * amplitude; }

    PartialBank<27> bank;
    Envelope envelope;
    double amplitude { 0.0 };
};

// ── Cuerdas ─────────────────────────────────────────────────────────────────

/** Sección de cuerdas: entrada lenta y cola larga, para las románticas.

    Tres capas desafinadas muy poco (±6 cents) más un vibrato suave. La entrada
    lenta es lo que la define: una cuerda frotada no empieza, *crece*. Con esto
    el instrumento deja de servir para tocar rápido, y está bien — es un
    colchón, no un piano.
*/
struct StringsVoice
{
    void start (int pitch, int velocity, double sampleRate) noexcept
    {
        const double fundamental = midiToHertz (pitch);
        const double velocityNorm = std::clamp (velocity / 127.0, 0.0, 1.0);

        std::uint32_t random = static_cast<std::uint32_t> (pitch) * 2654435761u + 3u;

        bank.clear();

        static constexpr double detuneCents[] = { -6.0, 0.0, 6.0 };

        for (double cents : detuneCents)
        {
            const double layerFrequency = fundamental * centsToRatio (cents);

            for (int harmonic = 1; harmonic <= 8; ++harmonic)
                bank.add (layerFrequency * harmonic,
                          std::pow (1.0 / harmonic, 1.5 - 0.3 * velocityNorm),
                          sampleRate, random);
        }

        bank.normaliseEnergy();

        // Entrada de 320 ms y cola de 900 ms.
        envelope.start (sampleRate, 0.320, 0.0, 1.0, 0.900);
        amplitude = 0.30 * (0.5 + 0.5 * velocityNorm);

        vibratoPhase = 0.0;
        vibratoInc = kTwoPi * 4.8 / sampleRate;
        vibratoDepth = 0.0;
    }

    void startRelease (double) noexcept { envelope.startRelease(); }

    double nextSample() noexcept
    {
        // El vibrato entra poco a poco, como el de un violinista que primero
        // coloca la nota y luego la mueve.
        if (vibratoDepth < 1.0)
            vibratoDepth += vibratoRampInc;

        vibratoPhase += vibratoInc;

        if (vibratoPhase >= kTwoPi)
            vibratoPhase -= kTwoPi;

        const double tremolo = 1.0 + 0.05 * vibratoDepth * std::sin (vibratoPhase);

        return bank.next() * envelope.next() * amplitude * tremolo;
    }

    double currentLevel() const noexcept { return envelope.level() * amplitude; }

    PartialBank<24> bank;
    Envelope envelope;
    double amplitude { 0.0 };
    double vibratoPhase { 0.0 };
    double vibratoInc { 0.0 };
    double vibratoDepth { 0.0 };
    static constexpr double vibratoRampInc = 1.0 / (0.6 * 48000.0);
};

// ── Vibráfono ───────────────────────────────────────────────────────────────

/** Vibráfono. Sale casi gratis: es un banco de parciales como el piano pero con
    las alturas **inarmónicas** de una barra metálica —aproximadamente 1, 4 y
    10 veces el fundamental— y el trémolo característico de los platillos
    girando dentro de los tubos.
*/
struct VibraphoneVoice
{
    void start (int pitch, int velocity, double sampleRate) noexcept
    {
        const double fundamental = midiToHertz (pitch);
        const double velocityNorm = std::clamp (velocity / 127.0, 0.0, 1.0);

        std::uint32_t random = static_cast<std::uint32_t> (pitch) * 1597334677u + 5u;

        bank.clear();
        bank.add (fundamental, 1.0, sampleRate, random);
        bank.add (fundamental * 3.98, 0.28 + 0.35 * velocityNorm, sampleRate, random);
        bank.add (fundamental * 10.6, 0.10 + 0.20 * velocityNorm, sampleRate, random);
        bank.normaliseEnergy();

        const double decaySeconds = 6.0 * std::pow (0.25, registerPosition (pitch));
        envelope.start (sampleRate, 0.001, decaySeconds, 0.0, 0.25);

        amplitude = 0.40 * std::pow (velocityNorm, 1.4);

        tremoloPhase = 0.0;
        tremoloInc = kTwoPi * 5.5 / sampleRate;
    }

    void startRelease (double) noexcept { envelope.startRelease(); }

    double nextSample() noexcept
    {
        tremoloPhase += tremoloInc;

        if (tremoloPhase >= kTwoPi)
            tremoloPhase -= kTwoPi;

        const double tremolo = 0.75 + 0.25 * std::sin (tremoloPhase);

        return bank.next() * envelope.next() * amplitude * tremolo;
    }

    double currentLevel() const noexcept { return envelope.level() * amplitude; }

    PartialBank<3> bank;
    Envelope envelope;
    double amplitude { 0.0 };
    double tremoloPhase { 0.0 };
    double tremoloInc { 0.0 };
};

// ── Marimba ───────────────────────────────────────────────────────────────

/** Marimba. Comparte planta con el vibráfono —una barra tiene parciales
    inarmónicos, no una serie— pero suena a otra cosa por tres motivos, y los
    tres importan más que las alturas:

    - **La madera se traga la energía.** Donde una barra de metal dura seis
      segundos, una de palisandro dura menos de uno.
    - **No hay trémolo.** Los tubos de la marimba resuenan, pero no gira nada
      dentro; el batido del vibráfono es un motor eléctrico y aquí no lo hay.
    - **Se oye la baqueta.** El golpe sobre madera tiene un "tock" de cuerpo que
      sin él deja el instrumento sonando a sintetizador de campanas.
*/
struct MarimbaVoice
{
    void start (int pitch, int velocity, double sampleRate) noexcept
    {
        const double fundamental = midiToHertz (pitch);
        const double velocityNorm = std::clamp (velocity / 127.0, 0.0, 1.0);
        const double t = registerPosition (pitch);

        std::uint32_t random = static_cast<std::uint32_t> (pitch) * 2246822507u + 17u;

        bank.clear();
        bank.add (fundamental, 1.0, sampleRate, random);
        bank.add (fundamental * 3.93, 0.22 + 0.26 * velocityNorm, sampleRate, random);
        bank.add (fundamental * 9.24, 0.06 + 0.14 * velocityNorm, sampleRate, random);
        bank.normaliseEnergy();

        // Un segundo escaso en el centro del teclado, y bastante menos arriba.
        const double decaySeconds = 1.10 * std::pow (0.30, t);
        envelope.start (sampleRate, 0.0008, decaySeconds, 0.0, 0.12);

        amplitude = 0.46 * std::pow (velocityNorm, 1.4);

        // El "tock" de la baqueta contra la madera: ruido corto y sordo. Sordo
        // de verdad —una barra de madera no tiene el brillo del metal— así que
        // el filtro va mucho más bajo que el martillo del piano.
        knockLevel = 0.30 * velocityNorm * velocityNorm;
        knockDecay = std::exp (-1.0 / (0.0045 * sampleRate));
        knockCoef = std::clamp ((350.0 + 900.0 * t) * kTwoPi / sampleRate, 0.0, 1.0);
        knockState = 0.0;
        noiseState = random | 1u;
    }

    void startRelease (double) noexcept { envelope.startRelease(); }

    double nextSample() noexcept
    {
        double sample = bank.next() * envelope.next() * amplitude;

        if (knockLevel > 1.0e-5)
        {
            noiseState ^= noiseState << 13;
            noiseState ^= noiseState >> 17;
            noiseState ^= noiseState << 5;

            const double white = static_cast<double> (noiseState) / 2147483648.0 - 1.0;
            knockState += (white - knockState) * knockCoef;

            sample += knockState * knockLevel;
            knockLevel *= knockDecay;
        }

        return sample;
    }

    double currentLevel() const noexcept { return envelope.level() * amplitude + knockLevel; }

    PartialBank<3> bank;
    Envelope envelope;
    double amplitude { 0.0 };
    double knockLevel { 0.0 };
    double knockDecay { 0.0 };
    double knockCoef { 0.0 };
    double knockState { 0.0 };
    std::uint32_t noiseState { 1 };
};

// ── Flauta ────────────────────────────────────────────────────────────────

/** Flauta. El instrumento más barato del catálogo y el que más se distingue de
    todos los demás: cuatro parciales contados.

    Un tubo abierto soplado suavemente da casi una senoide, y ahí está la
    trampa —una senoide sola no suena a flauta, suena a prueba de audio. Lo que
    la convierte en instrumento es **el aire**: un soplo de ruido filtrado que
    entra de golpe y luego se queda de fondo. Sin él no hay flauta.

    Sirve bien para practicar una melodía sola: no tiene cola que tape los
    errores, y una nota mal dada se oye desnuda.
*/
struct FluteVoice
{
    void start (int pitch, int velocity, double sampleRate) noexcept
    {
        const double fundamental = midiToHertz (pitch);
        const double velocityNorm = std::clamp (velocity / 127.0, 0.0, 1.0);

        std::uint32_t random = static_cast<std::uint32_t> (pitch) * 1103515245u + 23u;

        bank.clear();
        bank.add (fundamental, 1.0, sampleRate, random);
        bank.add (fundamental * 2.0, 0.09 + 0.13 * velocityNorm, sampleRate, random);
        bank.add (fundamental * 3.0, 0.03 + 0.06 * velocityNorm, sampleRate, random);
        bank.add (fundamental * 4.0, 0.010 + 0.025 * velocityNorm, sampleRate, random);
        bank.normaliseEnergy();

        envelope.start (sampleRate, 0.085, 0.0, 1.0, 0.090);
        amplitude = 0.52 * (0.55 + 0.45 * velocityNorm);

        // El aire: fuerte durante el ataque y un hilo después. Filtrado alto,
        // porque el soplo de una flauta es siseo, no retumbe.
        breathLevel = 0.55;
        breathFloor = 0.055 + 0.045 * velocityNorm;
        breathDecay = std::exp (-1.0 / (0.070 * sampleRate));
        breathCoef = std::clamp (3500.0 * kTwoPi / sampleRate, 0.0, 1.0);
        breathState = 0.0;
        noiseState = random | 1u;

        vibratoPhase = 0.0;
        vibratoInc = kTwoPi * 5.2 / sampleRate;
        vibratoDepth = 0.0;
        vibratoRampInc = 1.0 / (0.45 * sampleRate);
    }

    void startRelease (double) noexcept { envelope.startRelease(); }

    double nextSample() noexcept
    {
        // El vibrato de un flautista no está desde la primera nota: aparece
        // cuando la nota ya se sostiene.
        if (vibratoDepth < 1.0)
            vibratoDepth += vibratoRampInc;

        vibratoPhase += vibratoInc;

        if (vibratoPhase >= kTwoPi)
            vibratoPhase -= kTwoPi;

        const double envelopeValue = envelope.next();
        const double tremolo = 1.0 + 0.055 * vibratoDepth * std::sin (vibratoPhase);

        noiseState ^= noiseState << 13;
        noiseState ^= noiseState >> 17;
        noiseState ^= noiseState << 5;

        const double white = static_cast<double> (noiseState) / 2147483648.0 - 1.0;
        breathState += (white - breathState) * breathCoef;

        if (breathLevel > breathFloor)
            breathLevel = breathFloor + (breathLevel - breathFloor) * breathDecay;

        return (bank.next() * tremolo + breathState * breathLevel) * envelopeValue * amplitude;
    }

    double currentLevel() const noexcept { return envelope.level() * amplitude; }

    PartialBank<4> bank;
    Envelope envelope;
    double amplitude { 0.0 };
    double breathLevel { 0.0 };
    double breathFloor { 0.0 };
    double breathDecay { 0.0 };
    double breathCoef { 0.0 };
    double breathState { 0.0 };
    std::uint32_t noiseState { 1 };
    double vibratoPhase { 0.0 };
    double vibratoInc { 0.0 };
    double vibratoDepth { 0.0 };
    double vibratoRampInc { 0.0 };
};

// ── Coro ──────────────────────────────────────────────────────────────────

/** Coro cantando "aah".

    La diferencia entre esto y las cuerdas no está en la envolvente sino en el
    espectro: una voz humana no tiene una caída lisa de armónicos, tiene
    **formantes** —zonas fijas del espectro que la garganta refuerza, y que se
    quedan donde están aunque cambies de nota. Eso es lo que hace que una vocal
    siga siendo la misma vocal en grave y en agudo, y es lo único que hace falta
    imitar para que un banco de senos deje de sonar a órgano y empiece a sonar a
    gente.

    Los tres picos de aquí (700, 1150 y 2800 Hz) son los de una "a" abierta.

    Dos capas apenas desafinadas dan el resto: un coro no es una voz más alta,
    son varias que nunca están exactamente juntas.
*/
struct ChoirVoice
{
    void start (int pitch, int velocity, double sampleRate) noexcept
    {
        const double fundamental = midiToHertz (pitch);
        const double velocityNorm = std::clamp (velocity / 127.0, 0.0, 1.0);

        std::uint32_t random = static_cast<std::uint32_t> (pitch) * 2891336453u + 29u;

        bank.clear();

        static constexpr double detuneCents[] = { -7.0, 7.0 };

        for (double cents : detuneCents)
        {
            const double layerFrequency = fundamental * centsToRatio (cents);

            for (int harmonic = 1; harmonic <= 16; ++harmonic)
            {
                const double frequency = layerFrequency * harmonic;
                bank.add (frequency, formantGain (frequency) / harmonic, sampleRate, random);
            }
        }

        bank.normaliseEnergy();

        envelope.start (sampleRate, 0.180, 0.0, 1.0, 0.350);
        amplitude = 0.34 * (0.5 + 0.5 * velocityNorm);

        vibratoPhase = 0.0;
        vibratoInc = kTwoPi * 5.0 / sampleRate;
        vibratoDepth = 0.0;
        vibratoRampInc = 1.0 / (0.8 * sampleRate);
    }

    void startRelease (double) noexcept { envelope.startRelease(); }

    double nextSample() noexcept
    {
        if (vibratoDepth < 1.0)
            vibratoDepth += vibratoRampInc;

        vibratoPhase += vibratoInc;

        if (vibratoPhase >= kTwoPi)
            vibratoPhase -= kTwoPi;

        const double tremolo = 1.0 + 0.045 * vibratoDepth * std::sin (vibratoPhase);

        return bank.next() * envelope.next() * amplitude * tremolo;
    }

    double currentLevel() const noexcept { return envelope.level() * amplitude; }

    /** Refuerzo de la garganta en una frecuencia dada. Tres campanas gaussianas
        sobre un suelo, que es la manera más barata que hay de dibujar una vocal
        sin montar filtros resonantes. */
    static double formantGain (double frequency) noexcept
    {
        const auto peak = [frequency] (double centre, double width, double gain)
        {
            const double d = (frequency - centre) / width;
            return gain * std::exp (-d * d);
        };

        return 0.10
             + peak (700.0, 260.0, 1.00)
             + peak (1150.0, 380.0, 0.62)
             + peak (2800.0, 700.0, 0.24);
    }

    PartialBank<32> bank;
    Envelope envelope;
    double amplitude { 0.0 };
    double vibratoPhase { 0.0 };
    double vibratoInc { 0.0 };
    double vibratoDepth { 0.0 };
    double vibratoRampInc { 0.0 };
};

// ── Los instrumentos ────────────────────────────────────────────────────────

// Los recortes de ganancia salen de medir el pico de una nota sola y llevarlos
// todos al del piano, que es la referencia porque es el que está afinado a
// oído. Si se toca una voz, hay que volver a medir: el test [catalogue] falla
// si el catálogo se desequilibra.

class Organ final : public PolyphonicInstrument<OrganVoice>
{
public:
    explicit Organ (int voices = 32) : PolyphonicInstrument (voices, "Órgano", 0.78) {}
};

class ElectricPiano final : public PolyphonicInstrument<ElectricPianoVoice>
{
public:
    explicit ElectricPiano (int voices = 32) : PolyphonicInstrument (voices, "Piano eléctrico", 0.94) {}
};

class Accordion final : public PolyphonicInstrument<AccordionVoice>
{
public:
    // Menos voces: cada una lleva 27 parciales. Un acordeón de verdad tampoco
    // pasa de unas diez notas a la vez, así que no se pierde nada.
    explicit Accordion (int voices = 16) : PolyphonicInstrument (voices, "Acordeón", 0.57) {}
};

class Strings final : public PolyphonicInstrument<StringsVoice>
{
public:
    explicit Strings (int voices = 16) : PolyphonicInstrument (voices, "Cuerdas", 0.62) {}
};

class Vibraphone final : public PolyphonicInstrument<VibraphoneVoice>
{
public:
    explicit Vibraphone (int voices = 32) : PolyphonicInstrument (voices, "Vibráfono", 0.75) {}
};

class Marimba final : public PolyphonicInstrument<MarimbaVoice>
{
public:
    explicit Marimba (int voices = 32) : PolyphonicInstrument (voices, "Marimba", 0.63) {}
};

class Flute final : public PolyphonicInstrument<FluteVoice>
{
public:
    explicit Flute (int voices = 16) : PolyphonicInstrument (voices, "Flauta", 0.50) {}
};

class Choir final : public PolyphonicInstrument<ChoirVoice>
{
public:
    // Doce voces con 32 parciales cada una. Un coro tampoco canta acordes de
    // veinte notas, así que el techo no se nota tocando.
    explicit Choir (int voices = 12) : PolyphonicInstrument (voices, "Coro", 0.47) {}
};

class Guitar final : public PolyphonicInstrument<GuitarVoice>
{
public:
    // Seis cuerdas tiene una guitarra; se dejan 16 porque con pedal las notas
    // se solapan y quedarse sin voces se oye como un corte.
    explicit Guitar (int voices = 16) : PolyphonicInstrument (voices, "Guitarra", 0.76) {}
};

class Harpsichord final : public PolyphonicInstrument<HarpsichordVoice>
{
public:
    explicit Harpsichord (int voices = 24) : PolyphonicInstrument (voices, "Clavecín", 0.68) {}
};

// ── Catálogo ────────────────────────────────────────────────────────────────

/** El catálogo.

    **Los números de aquí son un contrato de persistencia**: `settings.json`
    guarda el instrumento elegido como entero, así que los nuevos se añaden
    siempre al final y ninguno cambia de valor. El orden en que se enseñan en
    la ventana es otra cosa y lo decide `allInstrumentIds()`. */
enum class InstrumentId
{
    piano, electricPiano, organ, accordion, strings, vibraphone,
    guitar, harpsichord, marimba, flute, choir
};

/** Todos, en el orden en que tiene sentido enseñarlos: primero los de teclado,
    luego los de fuelle y cuerda pulsada, luego los que sostienen y al final los
    de percusión y viento.

    Es la **única** lista del catálogo. La ventana construye el desplegable con
    ella, los ajustes validan con ella y los tests recorren con ella, de modo
    que un instrumento nuevo no se puede quedar sin probar por olvido. */
const std::vector<InstrumentId>& allInstrumentIds();

/** True si el entero guardado en los ajustes corresponde a un instrumento.
    Un `settings.json` de una versión más nueva no debe romper nada. */
bool isValidInstrumentId (int value);

/** Nombre para la interfaz. En UTF-8. */
juce::String instrumentName (InstrumentId id);

/** Crea el instrumento. Fuera del hilo de audio: aquí sí se asigna memoria. */
std::unique_ptr<IInstrument> createInstrument (InstrumentId id);

/** Reverberación recomendada por defecto para cada uno, 0..1. Un piano seco se
    oye bien; una sección de cuerdas sin sala suena a juguete. */
float defaultReverbFor (InstrumentId id);

} // namespace keyla::core
