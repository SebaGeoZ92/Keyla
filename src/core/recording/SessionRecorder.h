#pragma once

#include "../midi/MidiEvent.h"

#include <cstdint>
#include <vector>

namespace keyla::core
{

/** Una nota tal y como se tocó, con su posición exacta en el stream.

    `onsetSample` es **fraccionario** a propósito: la precisión de medida no
    está limitada por el tamaño del buffer (invariante 3). Con buffers de 3 ms
    se mide timing muy por debajo del milisegundo, y esa diferencia es la que
    separa un informe útil de uno decorativo.
*/
struct RecordedNote
{
    int pitch { 0 };
    int velocity { 0 };
    int channel { 1 };
    double onsetSample { 0.0 };
    double offsetSample { -1.0 };       // negativo = todavía sonando
    bool sustainedByPedal { false };

    bool isFinished() const noexcept { return offsetSample >= 0.0; }
    double durationSamples() const noexcept { return isFinished() ? offsetSample - onsetSample : 0.0; }
};

/** Un grupo de notas que se tocaron "a la vez".

    Notas que deberían sonar simultáneas nunca lo son. Sin agrupar, un acorde
    arpegiado se reporta como tres errores de timing en vez de como un acorde
    tocado un poco abierto (doc 01 §2.2).

    ### El límite de esto, dicho claro

    **No existe una tolerancia que separe todos los acordes de todos los pasajes
    rápidos**, porque un pasaje suficientemente veloz *es* indistinguible de un
    acorde arpegiado si sólo se mira el tiempo. Con 35 ms, una escala más rápida
    de unas 28 notas por segundo empieza a agruparse en acordes. Eso es un límite
    físico del criterio, no un fallo que se pueda arreglar afinando el número.

    Se eligen 35 y no los 50 del doc 01 porque la apertura de un acorde humano
    rara vez pasa de 30 ms, y bajar de 50 a 35 compra bastante margen frente a
    las escalas rápidas sin perder acordes reales. Cuando exista el modelo de
    partitura completo, esta decisión la podrá tomar el contexto —si lo escrito
    es un acorde, agrúpalo— en vez del reloj.
*/
struct PlayedEvent
{
    std::vector<RecordedNote> notes;

    /** El ataque del grupo es el de su nota **más temprana**, no la media: al
        tocar un acorde la mano llega escalonada y lo que marca el tiempo
        musical es la primera que suena. */
    double onsetSample() const noexcept;
    double meanVelocity() const noexcept;
    bool contains (int pitch) const noexcept;
};

/** Un *tap* sobre el flujo de eventos, no un motor (doc 02 §1).

    Guarda todo lo que llega, crudo, con su sello temporal. Grabar crudo es
    gratis y evita tener que repetir sesiones dentro de un año para analizar
    algo en lo que hoy no se ha pensado (doc 02 §4).

    Vive en el dominio de sesión: aquí sí se puede asignar memoria.
*/
class SessionRecorder
{
public:
    void start (double sampleRate);
    void stop (double endSample);
    bool isRecording() const noexcept { return recording; }

    void noteOn (int pitch, int velocity, double exactSample, int channel = 1);
    void noteOff (int pitch, double exactSample);
    void setSustainPedal (int value, double exactSample);

    const std::vector<RecordedNote>& notes() const noexcept { return recorded; }
    double sampleRate() const noexcept { return rate; }
    double lengthSamples() const noexcept;

    /** Las notas agrupadas en eventos con la tolerancia dada. */
    std::vector<PlayedEvent> groupIntoEvents (double toleranceSeconds = 0.035) const;

    void clear();

private:
    std::vector<RecordedNote> recorded;
    double rate { 48000.0 };
    double startedAt { 0.0 };
    double stoppedAt { 0.0 };
    bool recording { false };
    bool sustainDown { false };
};

} // namespace keyla::core
