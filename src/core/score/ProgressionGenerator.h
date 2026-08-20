#pragma once

#include "Exercise.h"
#include "../music/ChordRecognizer.h"

#include <vector>

namespace keyla::core
{

/** Progresiones de acordes de las que salen la mayoría de las canciones.

    La lista es corta y cada una está por un motivo, no por completar un
    catálogo: son las que aparecen una y otra vez y las que hacen que, en cuanto
    las tienes en los dedos, puedas acompañar cosas de verdad.
*/
enum class ProgressionId
{
    popIVviIV,      // I–V–vi–IV : media radio del último medio siglo
    doowop,         // I–vi–IV–V
    twoFiveOne,     // ii–V–I : la cadencia del jazz
    canon,          // I–V–vi–iii–IV–I–IV–V
    minorPop,       // i–VI–III–VII
    andalusian,     // i–VII–VI–V : el bajo que baja
    twelveBarBlues,
    cumbia          // i–iv–V–i : el bucle de la cumbia clásica
};

/** Cómo se colocan las notas de cada acorde en el teclado. */
enum class Voicing
{
    /** Siempre en estado fundamental. Sirve para ver la estructura, pero al
        tocarla la mano pega saltos enormes que nadie da en la vida real. */
    rootPosition,

    /** Se elige de cada acorde la inversión **más cercana** a lo que la mano ya
        tenía. Es como se tocan las progresiones de verdad: los dedos apenas se
        mueven y el oído oye las voces conducirse en vez de saltar.

        Esto es lo que convierte la progresión en un ejercicio de piano en vez
        de en una lista de acordes. */
    smoothVoiceLeading,

    /** Enlace suave en la derecha y la fundamental en la izquierda, una o dos
        octavas por debajo. Es la forma de acompañar más común que existe. */
    withLeftHandBass
};

struct ProgressionRequest
{
    int tonicPitch { 60 };
    ProgressionId id { ProgressionId::popIVviIV };
    Voicing voicing { Voicing::smoothVoiceLeading };
    Hand hand { Hand::right };
    double beatsPerChord { 4.0 };
    int repeats { 2 };
    bool useSevenths { false };
};

/** Un grado de la progresión: qué fundamental respecto a la tónica y de qué
    calidad. Se guarda así y no como alturas ya calculadas para que cambiar de
    tonalidad sea cambiar un número. */
struct ProgressionStep
{
    int semitonesFromTonic { 0 };
    ChordQuality quality { ChordQuality::major };
    juce::String degreeLabel;       // "I", "vi", "V7"...
};

std::vector<ProgressionStep> progressionSteps (ProgressionId id, bool useSevenths);

juce::String progressionName (ProgressionId id);
juce::String progressionDegrees (ProgressionId id);      // "I – V – vi – IV"
juce::String voicingName (Voicing voicing);

Exercise generateProgression (const ProgressionRequest& request);

} // namespace keyla::core
