// El importador es la respuesta al riesgo del doc 01 §2.4: sin él, Keyla es un
// motor excelente con nueve ejercicios escritos a mano.
//
// Los ficheros de prueba se construyen aquí mismo en memoria, así que no hay
// que versionar binarios ni depender de que exista un MIDI concreto en disco.

#include <core/score/MidiFileImporter.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace keyla::core;
using Catch::Matchers::WithinAbs;

namespace
{
    constexpr short ticksPerBeat = 960;

    struct NoteSpec
    {
        int pitch;
        double startBeat;
        double lengthBeats;
        int channel { 1 };
    };

    /** Escribe un SMF en memoria y lo devuelve listo para importar. */
    juce::MemoryBlock buildMidiFile (const std::vector<std::vector<NoteSpec>>& tracks,
                                     double tempoBpm = 120.0,
                                     int meterNumerator = 4,
                                     int meterDenominator = 4)
    {
        juce::MidiFile file;
        file.setTicksPerQuarterNote (ticksPerBeat);

        // Pista de metadatos: tempo y compás.
        juce::MidiMessageSequence meta;
        meta.addEvent (juce::MidiMessage::tempoMetaEvent (
                           static_cast<int> (60000000.0 / tempoBpm)));
        meta.addEvent (juce::MidiMessage::timeSignatureMetaEvent (meterNumerator, meterDenominator));
        file.addTrack (meta);

        for (const auto& track : tracks)
        {
            juce::MidiMessageSequence sequence;

            for (const auto& note : track)
            {
                sequence.addEvent (juce::MidiMessage::noteOn (note.channel, note.pitch,
                                                              static_cast<juce::uint8> (100))
                                       .withTimeStamp (note.startBeat * ticksPerBeat));
                sequence.addEvent (juce::MidiMessage::noteOff (note.channel, note.pitch)
                                       .withTimeStamp ((note.startBeat + note.lengthBeats) * ticksPerBeat));
            }

            sequence.updateMatchedPairs();
            file.addTrack (sequence);
        }

        juce::MemoryBlock block;
        juce::MemoryOutputStream out (block, false);
        file.writeTo (out);
        out.flush();

        return block;
    }

    MidiImportResult importBlock (const juce::MemoryBlock& block,
                                  const MidiImportOptions& options = {})
    {
        juce::MemoryInputStream stream (block, false);
        return importMidiFile (stream, options);
    }
}

TEST_CASE ("Una melodía simple se importa en orden", "[midi][import]")
{
    const auto block = buildMidiFile ({ { { 60, 0.0, 1.0 }, { 62, 1.0, 1.0 },
                                          { 64, 2.0, 1.0 }, { 65, 3.0, 1.0 } } });

    const auto result = importBlock (block);

    REQUIRE (result.ok);
    REQUIRE (result.exercise.events.size() == 4);

    const std::vector<int> expected { 60, 62, 64, 65 };

    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        CHECK (result.exercise.events[i].pitches.size() == 1);
        CHECK (result.exercise.events[i].pitches.front() == expected[i]);
        CHECK_THAT (result.exercise.events[i].onsetBeat, WithinAbs (static_cast<double> (i), 1e-6));
    }
}

TEST_CASE ("El tempo y el compás salen del fichero", "[midi][import]")
{
    const auto block = buildMidiFile ({ { { 60, 0.0, 1.0 } } }, 84.0, 3, 4);
    const auto result = importBlock (block);

    REQUIRE (result.ok);
    CHECK_THAT (result.tempoBpm, WithinAbs (84.0, 0.5));
    CHECK (result.meter.numerator == 3);
    CHECK (result.meter.denominator == 4);
}

TEST_CASE ("Las notas simultáneas se importan como un acorde", "[midi][import][chord]")
{
    const auto block = buildMidiFile ({ { { 60, 0.0, 2.0 }, { 64, 0.0, 2.0 }, { 67, 0.0, 2.0 },
                                          { 65, 2.0, 1.0 } } });

    const auto result = importBlock (block);

    REQUIRE (result.ok);
    REQUIRE (result.exercise.events.size() == 2);
    CHECK (result.exercise.events[0].pitches.size() == 3);
    CHECK (result.exercise.events[0].isChord());
    CHECK (result.exercise.events[1].pitches.size() == 1);
}

TEST_CASE ("Las alturas del acorde salen ordenadas, con el bajo primero", "[midi][import][chord]")
{
    // El resto del sistema —el cifrado, la alineación— asume que la primera es
    // la más grave. Importarlas en el orden del fichero rompería esa suposición
    // en silencio.
    const auto block = buildMidiFile ({ { { 67, 0.0, 1.0 }, { 60, 0.0, 1.0 }, { 64, 0.0, 1.0 } } });

    const auto result = importBlock (block);

    REQUIRE (result.ok);
    REQUIRE (result.exercise.events.size() == 1);

    const auto& pitches = result.exercise.events.front().pitches;
    REQUIRE (pitches.size() == 3);
    CHECK (pitches[0] == 60);
    CHECK (pitches[1] == 64);
    CHECK (pitches[2] == 67);
}

TEST_CASE ("El silencio inicial no se hace esperar al alumno", "[midi][import]")
{
    // Un fichero con dos compases de cuenta atrás no debe obligar a esperarlos
    // cada vez que se practica.
    const auto block = buildMidiFile ({ { { 60, 8.0, 1.0 }, { 62, 9.0, 1.0 } } });

    const auto result = importBlock (block);

    REQUIRE (result.ok);
    CHECK_THAT (result.exercise.events.front().onsetBeat, WithinAbs (0.0, 1e-6));
    CHECK_THAT (result.exercise.events.back().onsetBeat, WithinAbs (1.0, 1e-6));
}

TEST_CASE ("Con dos pistas, la más aguda es la mano derecha", "[midi][import][hands]")
{
    const auto block = buildMidiFile ({
        { { 72, 0.0, 1.0 }, { 74, 1.0, 1.0 } },     // aguda
        { { 48, 0.0, 1.0 }, { 50, 1.0, 1.0 } }      // grave
    });

    const auto result = importBlock (block);

    REQUIRE (result.ok);
    CHECK (result.handSeparation == HandSeparation::byTrack);

    for (const auto& event : result.exercise.events)
    {
        for (auto pitch : event.pitches)
        {
            if (pitch >= 72)
                CHECK ((event.hand == Hand::right || event.hand == Hand::both));
            else
                CHECK ((event.hand == Hand::left || event.hand == Hand::both));
        }
    }
}

TEST_CASE ("El orden de las pistas no decide la mano; la altura sí", "[midi][import][hands]")
{
    // Muchos ficheros traen la mano izquierda primero. Fiarse del orden sería
    // acertar la mitad de las veces.
    const auto block = buildMidiFile ({
        { { 40, 0.0, 1.0 }, { 43, 1.0, 1.0 } },     // grave, pero va primera
        { { 76, 0.0, 1.0 }, { 79, 1.0, 1.0 } }
    });

    // Se comprueba por la consecuencia observable —qué sale al pedir cada
    // mano— y no por la etiqueta del evento: las dos manos tocan a la vez, así
    // que sus notas caen en el mismo evento y su etiqueta es "las dos".
    MidiImportOptions leftOnly;
    leftOnly.hands = Hand::left;

    const auto left = importBlock (block, leftOnly);

    REQUIRE (left.ok);
    CHECK (left.exercise.highestPitch() <= 43);

    MidiImportOptions rightOnly;
    rightOnly.hands = Hand::right;

    const auto right = importBlock (block, rightOnly);

    REQUIRE (right.ok);
    CHECK (right.exercise.lowestPitch() >= 76);
}

TEST_CASE ("Un acorde a dos manos se etiqueta como de las dos", "[midi][import][hands]")
{
    // Al agrupar notas simultáneas de manos distintas, el evento pasa a ser de
    // las dos y **se pierde a qué mano pertenece cada altura**. Es una pérdida
    // real y consciente: el modelo tiene una mano por evento, no una por nota.
    // Hoy no importa —para practicar una mano se filtra antes de agrupar— pero
    // hará falta el día que se pinte la digitación de cada mano por separado.
    const auto block = buildMidiFile ({
        { { 76, 0.0, 1.0 } },
        { { 40, 0.0, 1.0 } }
    });

    const auto result = importBlock (block);

    REQUIRE (result.ok);
    REQUIRE (result.exercise.events.size() == 1);
    CHECK (result.exercise.events.front().hand == Hand::both);
    CHECK (result.exercise.events.front().pitches.size() == 2);
}

TEST_CASE ("Con una sola pista ancha se parte por altura y se avisa", "[midi][import][hands]")
{
    // Partir por altura es una conjetura nuestra, no un dato del fichero, y hay
    // que decirlo para que se pueda corregir (doc 02 §6).
    const auto block = buildMidiFile ({ { { 40, 0.0, 1.0 }, { 44, 0.5, 1.0 },
                                          { 72, 1.0, 1.0 }, { 76, 1.5, 1.0 } } });

    const auto result = importBlock (block);

    REQUIRE (result.ok);
    CHECK (result.handSeparation == HandSeparation::bySplitPoint);
    CHECK (handSeparationDescription (result.handSeparation).containsIgnoreCase ("revís"));
}

TEST_CASE ("Lo que cabe en una mano no se parte en dos", "[midi][import][hands]")
{
    // Inventarse una separación de manos en una melodía de una octava sería
    // peor que no separar nada.
    const auto block = buildMidiFile ({ { { 60, 0.0, 1.0 }, { 62, 1.0, 1.0 },
                                          { 64, 2.0, 1.0 }, { 67, 3.0, 1.0 } } });

    const auto result = importBlock (block);

    REQUIRE (result.ok);
    CHECK (result.handSeparation == HandSeparation::singleHand);

    for (const auto& event : result.exercise.events)
        CHECK (event.hand == Hand::right);
}

TEST_CASE ("Se puede importar sólo una mano", "[midi][import][hands]")
{
    const auto block = buildMidiFile ({
        { { 72, 0.0, 1.0 }, { 74, 1.0, 1.0 }, { 76, 2.0, 1.0 } },
        { { 48, 0.0, 1.0 }, { 50, 1.0, 1.0 } }
    });

    MidiImportOptions options;
    options.hands = Hand::right;

    const auto result = importBlock (block, options);

    REQUIRE (result.ok);
    CHECK (result.exercise.events.size() == 3);
    CHECK (result.exercise.lowestPitch() >= 72);
}

TEST_CASE ("El importador no inventa digitación", "[midi][import]")
{
    // MIDI no la trae, y ponerla a ojo sería enseñar a tocar mal (doc 02 §6).
    const auto block = buildMidiFile ({ { { 60, 0.0, 1.0 }, { 62, 1.0, 1.0 } } });
    const auto result = importBlock (block);

    REQUIRE (result.ok);

    for (const auto& event : result.exercise.events)
        CHECK (event.fingers.empty());

    CHECK (result.exercise.hint.containsIgnoreCase ("digitaci"));
}

TEST_CASE ("Un fichero larguísimo se recorta y se dice", "[midi][import]")
{
    std::vector<NoteSpec> many;

    for (int i = 0; i < 300; ++i)
        many.push_back ({ 60 + (i % 12), static_cast<double> (i), 0.5 });

    const auto block = buildMidiFile ({ many });

    MidiImportOptions options;
    options.maxEvents = 50;

    const auto result = importBlock (block, options);

    REQUIRE (result.ok);
    CHECK (result.truncated);
    CHECK (static_cast<int> (result.exercise.events.size()) <= 50);
    CHECK (result.message.isNotEmpty());
}

TEST_CASE ("Un fichero que no es MIDI no revienta nada", "[midi][import]")
{
    const char* garbage = "esto no es un fichero MIDI, ni de lejos";
    juce::MemoryInputStream stream (garbage, std::strlen (garbage), false);

    const auto result = importMidiFile (stream);

    CHECK (! result.ok);
    CHECK (result.message.isNotEmpty());
    CHECK (result.exercise.isEmpty());
}

TEST_CASE ("Un MIDI sin notas se rechaza con un motivo", "[midi][import]")
{
    const auto block = buildMidiFile ({ {} });
    const auto result = importBlock (block);

    CHECK (! result.ok);
    CHECK (result.message.containsIgnoreCase ("nota"));
}

TEST_CASE ("Lo importado se puede practicar tal cual", "[midi][import][integration]")
{
    // La prueba de que hay un solo modelo interno y dos fuentes (doc 02 §6): lo
    // que sale del importador entra en el mismo sitio que lo que sale del
    // generador de escalas.
    const auto block = buildMidiFile ({ { { 60, 0.0, 1.0 }, { 64, 1.0, 1.0 }, { 67, 2.0, 1.0 } } });
    auto result = importBlock (block);

    REQUIRE (result.ok);

    // Cabe en el teclado, o se ajusta.
    const auto fit = fitToKeyboardRange (result.exercise, 36, 84);
    CHECK (fit.outcome != RangeFit::Outcome::doesNotFit);

    CHECK (result.exercise.totalNotes() == 3);
    CHECK (result.exercise.lowestPitch() == 60);
    CHECK (result.exercise.highestPitch() == 67);
}
