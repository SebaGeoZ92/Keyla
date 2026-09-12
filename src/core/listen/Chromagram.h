#pragma once

// De audio a las doce notas. El primer paso de "Keyla escucha": convertir una
// señal en cuánta energía hay en cada clase de altura.

#include <array>
#include <vector>

namespace keyla::core
{

/** Energía por clase de altura, Do = 0. Normalizado a máximo 1. */
struct Chroma
{
    std::array<double, 12> bins {};

    /** Energía total del fotograma antes de normalizar. Sirve para saber si
        había música o silencio: un cromagrama de silencio normalizado parece
        un acorde perfectamente válido, y esa es la forma más fácil de que
        Keyla se invente acordes en los huecos de una canción. */
    double energy { 0.0 };

    bool isSilent() const noexcept { return energy < 1.0e-4; }
};

/** Análisis de Q constante.

    La tentación es hacer una FFT y repartir sus bins entre las doce notas, y
    está mal por un motivo concreto: la FFT reparte la frecuencia en trozos
    **iguales**, y la música no. Entre Do1 y Do#1 hay 4 Hz; entre Do6 y Do#6 hay
    62. Una FFT con resolución suficiente para separar el grave desperdicia
    miles de bins en el agudo, y una con resolución razonable en el agudo mete
    tres semitonos graves en el mismo bin.

    Aquí se hace al revés: **una ventana distinta por nota**, todas con el mismo
    número de ciclos. Eso es lo que significa "Q constante", y es lo que hace que
    la precisión relativa sea la misma en todo el teclado. El precio es que las
    notas graves necesitan ventanas largas —un La1 de 55 Hz necesita casi
    setecientos milisegundos para distinguirse de su vecino— y por eso el bajo
    siempre sale más borroso que el resto. No es un defecto que se pueda
    arreglar: es el compromiso entre tiempo y frecuencia, y conviene saber que
    está ahí antes de culpar al código.

    Esto **no** va en el hilo de audio: una ventana de 32 768 samples no cabe en
    un bloque de 144. El hilo de audio entrega samples por una FIFO y esto corre
    en un hilo de análisis (mismo patrón que el invariante 4).
*/
class ChromaAnalyser
{
public:
    struct Options
    {
        /** A1..C7. Por debajo de A1 la ventana necesaria se va de las manos, y
            por encima de C7 sólo quedan armónicos: ninguna de las dos zonas
            aporta al reconocimiento de acordes y las dos meten ruido. */
        int lowestPitch { 33 };
        int highestPitch { 96 };

        /** Ciclos por ventana. Más ciclos = más resolución en frecuencia y más
            retardo. El valor sale de medir, no de la teoría — ver los tests. */
        double cyclesPerBin { 40.0 };

        /** Techo de la ventana, en samples. Limita lo que pueden pedir las
            notas graves y acota la memoria y el coste. */
        int maxWindow { 32768 };

        /** Cada cuántos samples se produce un fotograma nuevo. */
        int hop { 4096 };

        /** Cuánto audio sin analizar se admite antes de descartar lo más viejo.

            El anillo guarda la ventana más larga **más** este margen. Sin
            margen, un empujón grande —una ráfaga del hilo de audio, o un
            fichero entero de golpe— sólo podría producir un fotograma, porque
            el resto del audio quedaría pisado antes de llegar a analizarse. */
        int maxBacklog { 65536 };
    };

    void prepare (double sampleRate, Options options = {});

    /** Añade audio mono. Se puede llamar con bloques de cualquier tamaño. */
    void push (const float* samples, int numSamples);

    /** Consume un fotograma si ya hay suficiente audio nuevo. */
    bool popFrame (Chroma& result);

    /** Energía por altura del último fotograma, del grave al agudo. Es lo que
        el cromagrama tira a la basura al plegar octavas, y hace falta para
        saber cuál es la nota del bajo — que en el reconocedor de acordes de
        Keyla es quien manda. */
    const std::vector<double>& pitchProfile() const noexcept { return profile; }

    int lowestPitch() const noexcept { return opts.lowestPitch; }

    /** Latencia del análisis: cuánto audio hay dentro de la ventana más larga.
        Se reporta porque un acorde detectado no es "ahora", es "hace esto". */
    double windowSeconds() const noexcept;

private:
    struct Bin
    {
        int pitch { 0 };
        int length { 0 };
        std::vector<float> cosKernel;   // ventana de Hann por el coseno
        std::vector<float> sinKernel;
    };

    Options opts;
    double rate { 48000.0 };

    std::vector<Bin> bins;
    std::vector<float> ring;
    int writePos { 0 };

    /** Samples empujados que todavía no ha consumido ningún fotograma. Cada
        fotograma consume exactamente un salto, **no todo lo pendiente**: si se
        vaciara entero, una ráfaga produciría un solo fotograma y el audio de en
        medio se perdería sin que nadie se enterara. */
    int pending { 0 };
    int filled { 0 };               // para no analizar basura al arrancar

    std::vector<double> profile;
};

} // namespace keyla::core
