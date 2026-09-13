// Reconocer lo que se toca es lógica pura sobre alturas: sin hardware, sin
// audio y sin UI. Por eso estos tests valen algo (doc 01 §1.8).

#include <core/music/ChordRecognizer.h>
#include <core/music/Pitch.h>

#include <catch2/catch_test_macros.hpp>

using namespace keyla::core;

namespace
{
    ChordMatch recognise (std::vector<int> notes)
    {
        return ChordRecognizer::recognise (notes);
    }
}

TEST_CASE ("El Do central es C4", "[music][pitch]")
{
    // Convenio Yamaha, el de casi todo el material de estudio.
    CHECK (noteName (60) == "C4");
    CHECK (noteName (69) == "A4");        // el la de 440 Hz
    CHECK (noteName (21) == "A0");        // la más grave de un piano de 88
    CHECK (noteName (108) == "C8");       // la más aguda
    CHECK (octaveOf (60) == 4);
}

TEST_CASE ("La misma tecla se escribe de dos maneras", "[music][pitch]")
{
    CHECK (noteName (61, Accidental::sharps) == "C#4");
    CHECK (noteName (61, Accidental::flats) == "Db4");
    CHECK (noteName (70, Accidental::sharps) == "A#4");
    CHECK (noteName (70, Accidental::flats) == "Bb4");
}

TEST_CASE ("Las clases de altura no se salen del rango con notas negativas", "[music][pitch]")
{
    CHECK (pitchClassOf (0) == 0);
    CHECK (pitchClassOf (11) == 11);
    CHECK (pitchClassOf (12) == 0);
    CHECK (pitchClassOf (-1) == 11);
}

TEST_CASE ("Los intervalos se nombran con su calidad", "[music][pitch]")
{
    CHECK (intervalShortName (0) == "1J");
    CHECK (intervalShortName (4) == "3M");
    CHECK (intervalShortName (3) == "3m");
    CHECK (intervalShortName (7) == "5J");
    CHECK (intervalShortName (6) == "TT");
    CHECK (intervalShortName (11) == "7M");

    // Y el orden de las notas no importa.
    CHECK (intervalName (60, 64) == intervalName (64, 60));
    CHECK (intervalName (60, 72) == juce::String (juce::CharPointer_UTF8 ("octava")));
}

TEST_CASE ("Tríadas en estado fundamental", "[music][chord]")
{
    CHECK (recognise ({ 60, 64, 67 }).symbol == "C");
    CHECK (recognise ({ 60, 63, 67 }).symbol == "Cm");
    CHECK (recognise ({ 60, 63, 66 }).symbol == "Cdim");
    CHECK (recognise ({ 60, 64, 68 }).symbol == "Caug");
    CHECK (recognise ({ 60, 65, 67 }).symbol == "Csus4");
    CHECK (recognise ({ 60, 62, 67 }).symbol == "Csus2");
}

TEST_CASE ("Séptimas y sextas", "[music][chord]")
{
    CHECK (recognise ({ 60, 64, 67, 70 }).symbol == "C7");
    CHECK (recognise ({ 60, 64, 67, 71 }).symbol == "Cmaj7");
    CHECK (recognise ({ 60, 63, 67, 70 }).symbol == "Cm7");
    CHECK (recognise ({ 60, 63, 67, 71 }).symbol == "Cm(maj7)");
    CHECK (recognise ({ 60, 63, 66, 69 }).symbol == "Cdim7");
    CHECK (recognise ({ 60, 63, 66, 70 }).symbol == "Cm7b5");
    CHECK (recognise ({ 60, 64, 67, 69 }).symbol == "C6");
    CHECK (recognise ({ 60, 63, 67, 69 }).symbol == "Cm6");
}

TEST_CASE ("Una séptima no se reporta como la tríada ignorando una nota", "[music][chord]")
{
    // El error clásico: probar plantillas de tres notas antes que las de cuatro
    // y quedarse tan ancho.
    const auto match = recognise ({ 60, 64, 67, 70 });

    REQUIRE (match.recognised);
    CHECK (match.quality == ChordQuality::dominant7);
    CHECK (match.quality != ChordQuality::major);
}

TEST_CASE ("Las inversiones se detectan y se cifran con el bajo", "[music][chord]")
{
    // Mi-Sol-Do: Do mayor con la tercera en el bajo.
    auto match = recognise ({ 64, 67, 72 });
    REQUIRE (match.recognised);
    CHECK (match.rootPitchClass == 0);
    CHECK (match.quality == ChordQuality::major);
    CHECK (match.inversion == 1);
    CHECK (match.symbol == "C/E");

    // Sol-Do-Mi: segunda inversión.
    match = recognise ({ 67, 72, 76 });
    REQUIRE (match.recognised);
    CHECK (match.inversion == 2);
    CHECK (match.symbol == "C/G");

    // Y en estado fundamental no se añade barra.
    match = recognise ({ 60, 64, 67 });
    CHECK (match.inversion == 0);
    CHECK (match.symbol == "C");
}

TEST_CASE ("Ante dos lecturas válidas, manda el bajo", "[music][chord]")
{
    // Estos conjuntos de notas admiten dos cifrados igual de correctos, y sin
    // tonalidad no hay forma de elegir. La regla es que la fundamental se busca
    // primero en la nota más grave: si tu izquierda está en Do, es un Do.
    //
    // No es un detalle de implementación — es la decisión que evita que el
    // cifrado baile de nombre según qué tabla interna se consulte antes.

    CHECK (recognise ({ 60, 62, 67 }).symbol == "Csus2");    // Do-Re-Sol
    CHECK (recognise ({ 55, 60, 62 }).symbol == "Gsus4");    // Sol-Do-Re: las mismas clases

    CHECK (recognise ({ 60, 64, 67, 69 }).symbol == "C6");   // Do-Mi-Sol-La
    CHECK (recognise ({ 57, 60, 64, 67 }).symbol == "Am7");  // La-Do-Mi-Sol: idénticas

    CHECK (recognise ({ 60, 63, 67, 69 }).symbol == "Cm6");
    CHECK (recognise ({ 57, 60, 63, 67 }).symbol == "Am7b5");
}

TEST_CASE ("Doblar notas en otras octavas no cambia el acorde", "[music][chord]")
{
    const auto plain = recognise ({ 60, 64, 67 });
    const auto doubled = recognise ({ 36, 48, 60, 64, 67, 72, 76 });

    REQUIRE (doubled.recognised);
    CHECK (doubled.symbol == plain.symbol);
    CHECK (doubled.rootPitchClass == plain.rootPitchClass);
}

TEST_CASE ("El orden en que llegan las notas da igual", "[music][chord]")
{
    CHECK (recognise ({ 67, 60, 64 }).symbol == "C");
    CHECK (recognise ({ 64, 60, 67 }).symbol == "C");
}

TEST_CASE ("La ortografía sigue a la fundamental", "[music][chord]")
{
    // Un acorde en Fa sostenido no se escribe con bemoles.
    CHECK (recognise ({ 66, 70, 73 }).symbol == "F#");

    // Y uno en Si bemol no se escribe con sostenidos.
    CHECK (recognise ({ 70, 74, 77 }).symbol == "Bb");
    CHECK (recognise ({ 63, 67, 70 }).symbol == "Eb");
}

TEST_CASE ("Cuando no sabe, lo dice en vez de inventarse un cifrado", "[music][chord]")
{
    // Dos notas no son un acorde.
    CHECK (! recognise ({ 60, 64 }).recognised);
    CHECK (! recognise ({ 60 }).recognised);
    CHECK (! recognise ({}).recognised);

    // Un racimo cromático no es nada reconocible.
    CHECK (! recognise ({ 60, 61, 62 }).recognised);

    // Cinco clases distintas se salen de lo que este reconocedor afirma.
    CHECK (! recognise ({ 60, 62, 64, 67, 71 }).recognised);

    // Y un cifrado inventado sería peor que ninguno: si no reconoce, no hay
    // símbolo que enseñar.
    CHECK (recognise ({ 60, 61, 62 }).symbol.isEmpty());
}

TEST_CASE ("El bajo se toma de la nota más grave, no de la primera", "[music][chord]")
{
    const auto match = recognise ({ 72, 64, 67 });   // Mi es la más grave

    REQUIRE (match.recognised);
    CHECK (match.bassPitchClass == 4);
    CHECK (match.inversion == 1);
}

TEST_CASE ("La descripción está en castellano y sin jerga", "[music][chord]")
{
    const auto match = recognise ({ 64, 67, 72 });

    REQUIRE (match.recognised);
    CHECK (match.description.contains ("Do"));
    CHECK (match.description.contains ("mayor"));
    CHECK (match.description.contains (juce::String (juce::CharPointer_UTF8 ("primera inversión"))));
}

TEST_CASE ("Los nombres en solfeo siguen la ortografia normal", "[music][names]")
{
    CHECK (spanishPitchClassName (0) == "Do");
    CHECK (spanishPitchClassName (10, conventionalAccidental (10, false)) == "Si bemol");
    CHECK (spanishPitchClassName (6, conventionalAccidental (6, true)) == "Fa sostenido");
    CHECK (spanishPitchClassName (1, conventionalAccidental (1, false)) == "Re bemol");
    CHECK (spanishPitchClassName (1, conventionalAccidental (1, true)) == "Do sostenido");
}
