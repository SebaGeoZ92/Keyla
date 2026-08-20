#pragma once

#include "Pitch.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace keyla::core
{

enum class ChordQuality
{
    unknown,
    major, minor, diminished, augmented,
    sus2, sus4,
    major6, minor6,
    dominant7, major7, minor7, minorMajor7, diminished7, halfDiminished7
};

struct ChordMatch
{
    bool recognised { false };

    int rootPitchClass { -1 };
    ChordQuality quality { ChordQuality::unknown };

    /** 0 = fundamental en el bajo, 1 = primera inversión, etc. */
    int inversion { 0 };

    /** La nota más grave, para el "/G" de los cifrados con bajo invertido. */
    int bassPitchClass { -1 };

    /** Cifrado listo para pantalla: "Cmaj7", "F#m/A". */
    juce::String symbol;

    /** En castellano y sin jerga: "Do mayor, primera inversión". */
    juce::String description;
};

/** Reconoce el acorde a partir de las alturas que suenan.

    Es una **función pura sobre un conjunto de notas** — sin estado, sin tiempo,
    sin hardware. Se puede testear con una lista de enteros, y por eso los tests
    de esto valen algo.

    Deliberadamente limitado a lo que se puede afirmar sin conocer la tonalidad:
    tríadas, séptimas, sextas y suspendidos, con inversiones. Nada de novenas ni
    de acordes alterados, porque a partir de ahí el mismo conjunto de notas
    admite varias lecturas y elegir una sin contexto es inventarse el análisis.
    Cuando no está seguro, dice que no lo sabe (`recognised == false`) en vez de
    adivinar: un cifrado inventado es peor que ninguno.
*/
class ChordRecognizer
{
public:
    /** `notes` son números MIDI, en cualquier orden y con repetidos. */
    static ChordMatch recognise (const std::vector<int>& notes);

    static juce::String qualitySymbol (ChordQuality quality);

    /** Semitonos desde la fundamental. Es la operación inversa del
        reconocedor, y vive aquí para que las plantillas de acorde estén en un
        solo sitio: el generador de progresiones consume exactamente las mismas
        definiciones con las que luego se cifra lo tocado. */
    static std::vector<int> intervalsFor (ChordQuality quality);

    static juce::String qualityDescription (ChordQuality quality);
};

} // namespace keyla::core
