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

/** Cuánto tiempo sonó un acorde. */
struct ChordDuration
{
    int rootPitchClass { -1 };
    ChordQuality quality { ChordQuality::unknown };
    double seconds { 0.0 };
};

/** La tonalidad a partir de **los acordes**, no de las notas sueltas.

    Sumar todas las notas oídas y compararlas con un perfil de tonalidad es el
    método clásico, y en música popular tiene una trampa: el acorde del quinto
    grado suena muchísimo —en cumbia, casi la mitad del tiempo— y arrastra el
    resultado hacia él. Una canción en Re mayor salía en La mayor.

    Con los acordes ya reconocidos se pregunta otra cosa, más musical: **en qué
    tonalidad encajan estos acordes**. D, A, Bm, Em y G sólo son todos de la
    misma familia en Re mayor (o en su relativa, Si menor). El empate entre
    relativas lo decide cuál de las dos tónicas ha sonado más. */
KeyEstimate keyFromChordDurations (const std::vector<ChordDuration>& chords);

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
        double sharpening { 1.5 };

        /** Por debajo de esto se dice "no lo sé" en vez de adivinar. */
        double minConfidence { 0.60 };
        double minMargin { 0.015 };

        /** Método antiguo: la nota más grave que llegue a esta fracción del
            máximo de **todo** el espectro. Sólo se usa con `useBassSalience`
            apagado, y se conserva para poder comparar contra él. */
        double bassThreshold { 0.30 };

        /** Buscar el bajo como se busca en una mezcla de verdad.

            El método antiguo fallaba por tres motivos que en música grabada
            pasan a la vez: la voz y el acordeón suenan más que el bajo y lo
            dejan por debajo de cualquier umbral relativo al total; el bajo de
            cumbia alterna fundamental y quinta, y fotograma a fotograma la
            mitad del tiempo el "bajo" es la quinta; y en muchas mezclas los
            armónicos del bajo suenan más que su propia nota.

            Con esto se mira **sólo la zona grave, con su propia escala**; cada
            nota se suma con sus armónicos, para quedarse con la que los produce
            y no con uno de ellos; y lo que sale tiene memoria. */
        bool useBassSalience { true };

        /** Nota MIDI más aguda que todavía cuenta como bajo. */
        int bassRangeTop { 48 };

        /** Cuánto recuerda el bajo de un fotograma al siguiente, 0..1. Con 0
            no hay memoria; con 0,8 una nota tiene que sonar varios fotogramas
            para desplazar a la anterior, que es lo que hace falta para que la
            quinta de paso de un bajo de cumbia no cambie el acorde. */
        double bassMemory { 0.8 };

        /** Exponente con que se agudiza la evidencia del bajo. Con 2, una
            quinta que suena al 60 % de la fundamental pesa un 36 %. */
        double bassSharpening { 2.0 };

        /** Por debajo de esta fracción del total, la zona grave se considera
            vacía y el bajo no opina. Sin esto, en un pasaje sin bajo cualquier
            resto de armónico en el grave decidiría el acorde. */
        double bassPresenceRatio { 0.05 };

        /** **El bajo decide entre lecturas que encajan; no convierte en acorde
            algo a lo que le faltan notas.**

            Sin esta condición el bajo sumaba sus puntos a cualquier candidato
            con esa fundamental, encajara o no. Con Do-Mi-Sol sonando y un Mi
            en el bajo, a Mi menor le falta el Si, pero en cuanto el peso del
            bajo pasaba de 0,08 el premio se imponía y un Do en primera
            inversión salía Mi menor — medido: con los pesos que mejor
            puntuaban en las canciones, el 100 % de las inversiones mal. Y era
            una trampa de las que sólo se ven buscándolas, porque en los
            acordes con la fundamental abajo un bajo fuerte siempre ayuda.

            El premio se escala por **la nota más floja del candidato**,
            relativa a la más fuerte del cromagrama: si todas sus notas suenan
            al menos a esta fracción, el bajo cuenta entero; si le falta una,
            no cuenta. 0 = sin condición, el comportamiento antiguo. */
        double bassFitGate { 0.35 };

        /** Cuánto pesa que el bajo sea la fundamental. Con `useBassSalience`
            es el peso de una evidencia **gradual** —cada fundamental candidata
            suma según cuánto suene en el grave—; con el método antiguo, un
            premio todo o nada a una sola nota. **El bajo manda**, la
            misma regla que ya gobierna el reconocedor de MIDI, y aquí no es un
            refinamiento sino una necesidad: Do#m7 y Mi6 son literalmente las
            mismas cuatro notas, así que sus plantillas empatan al decimosexto
            decimal y sin nada que desempate el reconocedor se queda mudo. Lo
            único que distingue esos dos acordes es cuál de las notas está
            abajo. Pequeño a propósito: sólo debe decidir empates, nunca
            imponerse a una lectura claramente mejor. */
        double bassIsRootBonus { 0.40 };

        /** Cuánto le cuesta a una lectura que no sea mayor o menor.

            Una séptima mayor, una sus4 o un disminuido necesitan **más
            evidencia** que una tríada, porque en música popular son mucho más
            raros — y porque en una mezcla la melodía siempre está añadiendo
            notas sueltas que encajan en una plantilla de cuatro. Sin esto, la
            voz cantando un Fa# sobre un Sol convierte el Sol en Solmaj7. */
        double complexQualityPenalty { 0.30 };

        /** Lo mismo para las séptimas (dominante, menor y mayor), aparte y
            normalmente más suave. No son raras: en boleros y baladas están por
            todas partes. Pero un castigo alto las borraría del todo, porque en
            una séptima limpia la cuarta nota sólo le saca 0,13 a la tríada. */
        double seventhQualityPenalty { 0.05 };

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

    /** Lo mismo con la evidencia del bajo gradual: cuánto suena cada clase de
        altura en el grave, de 0 a 1. `bassPitchClass` es sólo para escribir la
        barra del cifrado ("C/E"). */
    static AudioChordEstimate estimate (const Chroma& chroma,
                                        const std::array<double, 12>& bassEvidence,
                                        int bassPitchClass,
                                        const Options& options);

    /** Cuánto suena cada clase de altura **como bajo**, de 0 a 1, a partir del
        perfil por altura. Todo ceros si el grave está vacío. Función pura. */
    static std::array<double, 12> bassSalience (const std::vector<double>& pitchProfile,
                                                int lowestPitch,
                                                const Options& options);

private:
    Options opts;

    AudioChordEstimate stable;
    AudioChordEstimate candidate;
    int agreement { 0 };

    std::array<double, 12> keyAccumulator {};
    std::array<double, 12> smoothedBass {};

    /** Fotogramas en que cada acorde fue el estable, por fundamental y
        calidad. Es de donde sale la tonalidad en cuanto hay acordes. */
    std::array<double, 12 * 16> chordFrames {};
    std::vector<AudioChordEstimate> history;
};

} // namespace keyla::core
