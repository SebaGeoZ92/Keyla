#pragma once

// De "qué acorde suena" a "dónde pongo los dedos". El último eslabón entre oír
// y tocar.

#include "../music/ChordRecognizer.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace keyla::core
{

/** Una forma concreta de tocar un acorde, ahora mismo, con la mano donde está. */
struct AccompanimentSuggestion
{
    bool valid { false };

    /** Notas MIDI para la derecha, de grave a aguda. */
    std::vector<int> rightHand;

    /** Fundamental para la izquierda. -1 si no se pide o no cabe. */
    int bassNote { -1 };

    /** Todas juntas, para encender el teclado de la pantalla. */
    std::vector<int> allNotes() const;

    /** "Fa3 La3 Do4, bajo Fa2" — leíble sin saber solfeo. */
    juce::String description;
};

/** Sugiere cómo acompañar lo que está sonando.

    No inventa armonía: recibe el acorde ya reconocido y sólo decide **dónde**
    tocarlo. Toda la gracia está en esa decisión, porque es la diferencia entre
    una lista de acordes y algo que se puede tocar.

    Guarda dónde quedó la mano en el acorde anterior y elige la inversión más
    cercana. Es la misma maquinaria que usan las progresiones del generador de
    ejercicios —literalmente las mismas funciones— y por el mismo motivo: en un
    acompañamiento real los dedos apenas se mueven, y saltar a fundamental en
    cada cambio es lo que delata a quien está leyendo cifrados en vez de tocar.

    Es una clase con memoria y no una función pura porque el enlace de voces
    **es** memoria: la mejor colocación de un Fa depende de qué venía antes.
*/
class AccompanimentCoach
{
public:
    struct Options
    {
        /** Alrededor de qué nota se coloca la derecha. Do central por defecto. */
        int centrePitch { 60 };

        /** Añadir la fundamental en la izquierda. Es la forma de acompañar más
            común que existe, y la que mejor suena con una canción sonando: el
            bajo del disco y el tuyo se refuerzan. */
        bool withLeftHandBass { true };
    };

    explicit AccompanimentCoach (Options options = {}) : opts (options) {}

    void setOptions (Options options) { opts = options; }

    /** Olvida dónde estaba la mano. Al cambiar de canción, arrastrar la
        posición anterior enlaza voces entre cosas que no tienen relación. */
    void reset();

    AccompanimentSuggestion suggest (int rootPitchClass, ChordQuality quality);

private:
    Options opts;
    std::vector<int> previousHand;
};

} // namespace keyla::core
