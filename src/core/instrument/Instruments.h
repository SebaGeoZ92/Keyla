#pragma once

// Los instrumentos de Keyla. Cada uno es una voz y una línea de typedef: toda
// la maquinaria de reparto, pedal y robo la pone PolyphonicInstrument.

#include "PolyphonicInstrument.h"
#include "Voices.h"

#include <juce_core/juce_core.h>

#include <memory>

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

// ── Catálogo ────────────────────────────────────────────────────────────────

enum class InstrumentId
{
    piano, electricPiano, organ, accordion, strings, vibraphone
};

/** Nombre para la interfaz. En UTF-8. */
juce::String instrumentName (InstrumentId id);

/** Crea el instrumento. Fuera del hilo de audio: aquí sí se asigna memoria. */
std::unique_ptr<IInstrument> createInstrument (InstrumentId id);

/** Reverberación recomendada por defecto para cada uno, 0..1. Un piano seco se
    oye bien; una sección de cuerdas sin sala suena a juguete. */
float defaultReverbFor (InstrumentId id);

} // namespace keyla::core
