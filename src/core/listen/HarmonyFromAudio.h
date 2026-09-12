#pragma once

// De las doce notas al cifrado. La segunda mitad de "Keyla escucha".

#include "Chromagram.h"

#include "../music/ChordRecognizer.h"

#include <juce_core/juce_core.h>

#include <deque>
#include <vector>

namespace keyla::core
{

/** Lo que Keyla cree estar oyendo, y **con cuánta seguridad**.

    La confianza no es decoración. Sacar acordes de audio no es como sacarlos de
    MIDI: en MIDI las notas son un hecho, aquí son una estimación sobre una
    mezcla donde hay batería, voz y reverberación. Un cifrado sin margen de
    error invita a creerlo, y equivocarse en silencio es justo lo que no puede
    hacer una herramienta de la que alguien va a aprender. */
struct AudioChordEstimate
{
    bool recognised { false };

    int rootPitchClass { -1 };
    int bassPitchClass { -1 };
    ChordQuality quality { ChordQuality::unknown };

    /** Parecido con la plantilla, 0..1. */
    double confidence { 0.0 };

    /** Distancia con la segunda mejor lectura. Es lo que de verdad dice si la
        respuesta es sólida: una confianza de 0,9 con un margen de 0,002
        significa que hay dos acordes empatados y se eligió uno a cara o cruz. */
    double margin { 0.0 };

    juce::String symbol;
    juce::String description;
};

/** La tonalidad estimada a partir de todo lo que se lleva oído. */
struct KeyEstimate
{
    bool recognised { false };
    int tonicPitchClass { -1 };
    bool minor { false };
    double confidence { 0.0 };
    juce::String name;          // "Sol mayor", "La menor"
};

/** Reconocimiento de acordes sobre audio.

    Dos decisiones que separan esto de "probar plantillas y quedarse con la
    mejor":

    - **El cromagrama se agudiza antes de comparar.** Una sola nota de piano no
      produce sólo su altura: produce su octava, su quinta, su tercera mayor y
      una séptima menor floja. Un Do solo se parece a un Do7 más de lo que
      parece razonable. Elevar el cromagrama a una potencia hunde los armónicos
      débiles frente a las notas que de verdad se están tocando.
    - **Hay que estar de acuerdo varias veces seguidas.** Los acordes duran
      compases y el análisis produce fotogramas diez veces por segundo. Cambiar
      el cifrado con cada fotograma da un cartel parpadeando que no sirve para
      tocar; exigir acuerdo sostenido cuesta un poco de retardo y a cambio lo
      que sale en pantalla se puede leer.
*/
class HarmonyListener
{
public:
    struct Options
    {
        /** Exponente con el que se agudiza el cromagrama. 1 = tal cual. */
        double sharpening { 2.0 };

        /** Por debajo de esto se dice "no lo sé" en vez de adivinar. */
        double minConfidence { 0.60 };
        double minMargin { 0.015 };

        /** Fracción del máximo a partir de la cual una altura cuenta como
            presente, para buscar el bajo. */
        double bassThreshold { 0.30 };

        /** Cuánto pesa que el bajo sea la fundamental. **El bajo manda**, la
            misma regla que ya gobierna el reconocedor de MIDI, y aquí no es un
            refinamiento sino una necesidad: Do#m7 y Mi6 son literalmente las
            mismas cuatro notas, así que sus plantillas empatan al decimosexto
            decimal y sin nada que desempate el reconocedor se queda mudo. Lo
            único que distingue esos dos acordes es cuál de las notas está
            abajo. Pequeño a propósito: sólo debe decidir empates, nunca
            imponerse a una lectura claramente mejor. */
        double bassIsRootBonus { 0.03 };

        /** Fotogramas seguidos que deben coincidir para cambiar el cifrado. */
        int framesToAgree { 3 };
    };

    explicit HarmonyListener (Options options = {}) : opts (options) {}

    void reset();

    /** Procesa un fotograma. Devuelve la estimación **estable** actual, que
        puede ser la misma que antes si el fotograma nuevo no convence. */
    AudioChordEstimate observe (const Chroma& chroma,
                                const std::vector<double>& pitchProfile,
                                int lowestPitch);

    const AudioChordEstimate& current() const noexcept { return stable; }

    KeyEstimate key() const;

    /** Los cifrados por los que se ha ido pasando, en orden y sin repetir el
        mismo dos veces seguidas. Es la progresión de la canción. */
    const std::vector<AudioChordEstimate>& progression() const noexcept { return history; }

    /** Reconocimiento de un solo fotograma, sin memoria ni suavizado.

        Función pura sobre doce números: se puede testear escribiendo un
        cromagrama a mano, igual que `ChordRecognizer` se testea con una lista
        de enteros. */
    static AudioChordEstimate estimate (const Chroma& chroma,
                                        int bassPitchClass,
                                        const Options& options);

private:
    Options opts;

    AudioChordEstimate stable;
    AudioChordEstimate candidate;
    int agreement { 0 };

    std::array<double, 12> keyAccumulator {};
    std::vector<AudioChordEstimate> history;
};

} // namespace keyla::core
