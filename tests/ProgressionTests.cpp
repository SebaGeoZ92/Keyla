// Una progresión no es una lista de acordes: es una lista de acordes **con una
// manera de tocarlos**. Lo que la convierte en un ejercicio de piano es el
// enlace de voces, y eso es lo que se comprueba aquí.

#include <core/music/ChordRecognizer.h>
#include <core/score/ProgressionGenerator.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using namespace keyla::core;

namespace
{
    /** Cuánto se mueve la mano entre dos acordes seguidos: la suma de las
        distancias dedo a dedo. Es la medida de lo cómodo que resulta tocar la
        progresión. */
    int handMovement (const Exercise& exercise)
    {
        int total = 0;

        for (std::size_t i = 1; i < exercise.events.size(); ++i)
        {
            const auto& before = exercise.events[i - 1].pitches;
            const auto& after = exercise.events[i].pitches;
            const auto common = std::min (before.size(), after.size());

            for (std::size_t n = 0; n < common; ++n)
                total += std::abs (static_cast<int> (before[n]) - static_cast<int> (after[n]));
        }

        return total;
    }

    /** Reconoce cada acorde del ejercicio con el mismo reconocedor que usa la
        pantalla: si el generador y el reconocedor no coinciden, uno de los dos
        está mal. */
    std::vector<juce::String> symbolsOf (const Exercise& exercise)
    {
        std::vector<juce::String> symbols;

        for (const auto& event : exercise.events)
        {
            std::vector<int> notes;

            for (auto pitch : event.pitches)
                notes.push_back (pitch);

            symbols.push_back (ChordRecognizer::recognise (notes).symbol);
        }

        return symbols;
    }
}

TEST_CASE ("La progresión pop en Do son los acordes que todo el mundo conoce", "[progression]")
{
    ProgressionRequest request;
    request.tonicPitch = 60;
    request.id = ProgressionId::popIVviIV;
    request.voicing = Voicing::rootPosition;
    request.repeats = 1;

    const auto exercise = generateProgression (request);

    REQUIRE (exercise.events.size() == 4);

    const auto symbols = symbolsOf (exercise);
    CHECK (symbols[0] == "C");
    CHECK (symbols[1] == "G");
    CHECK (symbols[2] == "Am");
    CHECK (symbols[3] == "F");
}

TEST_CASE ("Cambiar de tonalidad es cambiar un número", "[progression]")
{
    ProgressionRequest request;
    request.tonicPitch = 67;            // Sol
    request.id = ProgressionId::popIVviIV;
    request.voicing = Voicing::rootPosition;
    request.repeats = 1;

    const auto symbols = symbolsOf (generateProgression (request));

    REQUIRE (symbols.size() == 4);
    CHECK (symbols[0] == "G");
    CHECK (symbols[1] == "D");
    CHECK (symbols[2] == "Em");
    CHECK (symbols[3] == "C");
}

TEST_CASE ("El enlace de voces mueve mucho menos la mano", "[progression][voicing]")
{
    // Éste es el test que justifica que exista el enlace de voces. En estado
    // fundamental la mano da saltos de una octava entre acordes; enlazando, los
    // dedos apenas se mueven — que es como se tocan las progresiones de verdad.
    ProgressionRequest plain;
    plain.tonicPitch = 60;
    plain.id = ProgressionId::popIVviIV;
    plain.voicing = Voicing::rootPosition;
    plain.repeats = 2;

    auto smooth = plain;
    smooth.voicing = Voicing::smoothVoiceLeading;

    const int plainMovement = handMovement (generateProgression (plain));
    const int smoothMovement = handMovement (generateProgression (smooth));

    INFO ("fundamental " << plainMovement << "  enlazado " << smoothMovement);
    CHECK (smoothMovement < plainMovement / 2);
}

TEST_CASE ("Enlazar no cambia los acordes, sólo cómo se colocan", "[progression][voicing]")
{
    // Una inversión sigue siendo el mismo acorde. Si el enlace cambiara la
    // armonía, el ejercicio enseñaría otra cosa distinta de la que dice.
    ProgressionRequest request;
    request.tonicPitch = 60;
    request.id = ProgressionId::popIVviIV;
    request.voicing = Voicing::smoothVoiceLeading;
    request.repeats = 1;

    const auto exercise = generateProgression (request);
    REQUIRE (exercise.events.size() == 4);

    const std::vector<int> expectedRoots { 0, 7, 9, 5 };     // C, G, Am, F

    for (std::size_t i = 0; i < expectedRoots.size(); ++i)
    {
        std::vector<int> notes;

        for (auto pitch : exercise.events[i].pitches)
            notes.push_back (pitch);

        const auto match = ChordRecognizer::recognise (notes);

        INFO ("acorde " << i << " -> " << match.symbol.toStdString());
        REQUIRE (match.recognised);
        CHECK (match.rootPitchClass == expectedRoots[i]);
    }
}

TEST_CASE ("El bajo en la izquierda va lejos de la derecha", "[progression][voicing]")
{
    // Si las manos se solapan, la progresión no se puede tocar. Y un bajo
    // demasiado cerca del acorde suena a barro, que es lo que ya sabemos que
    // pasa amontonando notas en el registro grave.
    ProgressionRequest request;
    request.tonicPitch = 60;
    request.id = ProgressionId::popIVviIV;
    request.voicing = Voicing::withLeftHandBass;
    request.repeats = 1;

    const auto exercise = generateProgression (request);

    for (const auto& event : exercise.events)
    {
        REQUIRE (event.pitches.size() >= 4);

        const int bass = event.pitches.front();
        const int nextUp = event.pitches[1];

        INFO ("bajo " << bass << " siguiente " << nextUp);
        CHECK (nextUp - bass >= 7);         // al menos una quinta de separación
        CHECK (event.hand == Hand::both);
    }
}

TEST_CASE ("El ii-V-I lleva séptimas siempre", "[progression]")
{
    // Sin la séptima, un ii-V-I no suena a ii-V-I: suena a tres acordes
    // seguidos. La tensión que resuelve es justo la nota que se le quitaría.
    ProgressionRequest request;
    request.tonicPitch = 60;
    request.id = ProgressionId::twoFiveOne;
    request.voicing = Voicing::rootPosition;
    request.repeats = 1;
    request.useSevenths = false;        // aunque no se pidan

    const auto exercise = generateProgression (request);

    for (const auto& event : exercise.events)
        CHECK (event.pitches.size() == 4);

    const auto symbols = symbolsOf (exercise);
    CHECK (symbols[0] == "Dm7");
    CHECK (symbols[1] == "G7");
    CHECK (symbols[2] == "Cmaj7");
}

TEST_CASE ("La dominante lleva séptima aunque el resto no", "[progression]")
{
    ProgressionRequest request;
    request.tonicPitch = 60;
    request.id = ProgressionId::popIVviIV;
    request.voicing = Voicing::rootPosition;
    request.repeats = 1;
    request.useSevenths = true;

    const auto symbols = symbolsOf (generateProgression (request));

    CHECK (symbols[0] == "Cmaj7");
    CHECK (symbols[1] == "G7");         // dominante, no Gmaj7
}

TEST_CASE ("El blues son doce compases", "[progression]")
{
    ProgressionRequest request;
    request.id = ProgressionId::twelveBarBlues;
    request.repeats = 1;

    const auto exercise = generateProgression (request);

    CHECK (exercise.events.size() == 12);
}

TEST_CASE ("La cumbia es menor y vuelve a la tónica", "[progression]")
{
    ProgressionRequest request;
    request.tonicPitch = 57;            // La menor
    request.id = ProgressionId::cumbia;
    request.voicing = Voicing::rootPosition;
    request.repeats = 1;

    const auto symbols = symbolsOf (generateProgression (request));

    REQUIRE (symbols.size() == 4);
    CHECK (symbols[0] == "Am");
    CHECK (symbols[1] == "Dm");
    CHECK (symbols[3] == "Am");         // cierra donde empezó
}

TEST_CASE ("Las repeticiones encadenan sin saltar al volver a empezar", "[progression][voicing]")
{
    // Al repetir el bucle, el último acorde tiene que enlazar con el primero
    // igual de bien que los de dentro. Reiniciar la mano en cada vuelta
    // produciría un salto justo en el punto donde más se nota.
    ProgressionRequest request;
    request.tonicPitch = 60;
    request.id = ProgressionId::popIVviIV;
    request.voicing = Voicing::smoothVoiceLeading;
    request.repeats = 3;

    const auto exercise = generateProgression (request);
    REQUIRE (exercise.events.size() == 12);

    // El salto en la juntura (evento 3 → 4) no debe ser peor que el mayor de
    // los saltos internos.
    auto jumpBetween = [&exercise] (std::size_t i)
    {
        const auto& before = exercise.events[i].pitches;
        const auto& after = exercise.events[i + 1].pitches;
        int total = 0;

        for (std::size_t n = 0; n < std::min (before.size(), after.size()); ++n)
            total += std::abs (static_cast<int> (before[n]) - static_cast<int> (after[n]));

        return total;
    };

    const int atSeam = jumpBetween (3);
    const int worstInside = std::max ({ jumpBetween (0), jumpBetween (1), jumpBetween (2) });

    INFO ("juntura " << atSeam << "  peor interno " << worstInside);
    CHECK (atSeam <= worstInside + 2);
}

TEST_CASE ("Todo se queda dentro de un piano de verdad", "[progression]")
{
    for (auto id : { ProgressionId::popIVviIV, ProgressionId::doowop,
                     ProgressionId::twoFiveOne, ProgressionId::canon,
                     ProgressionId::minorPop, ProgressionId::andalusian,
                     ProgressionId::twelveBarBlues, ProgressionId::cumbia })
    {
        for (auto voicing : { Voicing::rootPosition, Voicing::smoothVoiceLeading,
                              Voicing::withLeftHandBass })
        {
            ProgressionRequest request;
            request.id = id;
            request.voicing = voicing;

            const auto exercise = generateProgression (request);

            INFO (exercise.name.toStdString());
            REQUIRE (! exercise.isEmpty());
            CHECK (exercise.lowestPitch() >= 21);
            CHECK (exercise.highestPitch() <= 108);
            CHECK (exercise.name.isNotEmpty());
        }
    }
}

TEST_CASE ("Los acordes duran lo que se pide y no se solapan", "[progression]")
{
    ProgressionRequest request;
    request.id = ProgressionId::popIVviIV;
    request.beatsPerChord = 2.0;
    request.repeats = 1;

    const auto exercise = generateProgression (request);

    for (std::size_t i = 0; i < exercise.events.size(); ++i)
    {
        CHECK (exercise.events[i].onsetBeat == static_cast<double> (i) * 2.0);
        CHECK (exercise.events[i].durationBeats == 2.0);
    }
}
