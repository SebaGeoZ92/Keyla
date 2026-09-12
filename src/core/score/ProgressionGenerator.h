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

// ── Colocación de acordes ───────────────────────────────────────────────────
//
// Estaban escondidas dentro del .cpp porque sólo las usaban las progresiones.
// En cuanto Keyla empezó a sacar acordes de una canción hizo falta lo mismo
// para acompañarla, y duplicar el enlace de voces habría sido duplicar también
// sus decisiones sutiles.

/** Coloca un acorde en el teclado eligiendo la inversión **más cercana** a
    donde la mano ya estaba. Con `previous` vacío elige la más centrada.

    Esto es lo que separa un acompañamiento de una lista de acordes: los dedos
    apenas se mueven entre acorde y acorde, en vez de saltar a fundamental cada
    vez. */
std::vector<int> voiceChordNear (int rootPitchClass, ChordQuality quality,
                                 const std::vector<int>& previous,
                                 int centrePitch = 60);

/** La fundamental para la izquierda: al menos una quinta por debajo de la
    derecha, para que las dos manos no se amontonen en el mismo sitio — que en
    el grave es exactamente como se consigue que un acorde suene a barro.
    Devuelve -1 si no cabe en el teclado. */
int bassNoteFor (int rootPitchClass, const std::vector<int>& rightHand);

} // namespace keyla::core
