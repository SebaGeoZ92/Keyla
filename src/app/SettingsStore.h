#pragma once

#include "AudioDeviceHost.h"

#include <core/instrument/Instruments.h>

#include <juce_core/juce_core.h>

namespace keyla::app
{

/** Lo que Keyla recuerda entre sesiones.

    Sin esto la aplicación no se puede usar en serio: cada vez que la abres hay
    que volver a elegir salida, buffer y puerto MIDI, y en un equipo donde el
    dispositivo predeterminado de Windows es un mezclador virtual eso significa
    volver a equivocarse cada día.

    Los dispositivos se guardan **por nombre**, nunca por índice (doc 01 §1.1).
    Un ajuste guardado que apunte a un dispositivo que ya no está no es un error:
    se ignora y se cae al comportamiento por defecto.
*/
struct Settings
{
    juce::String audioOutputName;
    int bufferSize { 128 };
    bool exclusive { true };

    juce::String midiInputName;

    /** Qué salida escucha Keyla cuando enciendes la escucha. Vacío = la
        predeterminada de Windows, que es por donde suena el navegador. No se
        arranca sola al abrir: encender una captura sin que nadie la haya pedido
        es justo el tipo de cosa que luego nadie sabe por qué está pasando. */
    juce::String listenDeviceName;

    core::InstrumentId instrument { core::InstrumentId::piano };
    float masterVolume { 0.8f };
    float reverbMix { 0.12f };
    float tremoloDepth { 0.0f };

    /** Qué CC mueve cada cosa. Por defecto los estándar, pero se reaprenden:
        hay controladores que no respetan ninguno. */
    int volumeController { 7 };
    int tremoloController { 1 };        // rueda de modulación
    int reverbController { 91 };        // envío a reverberación

    double tempoBpm { 90.0 };

    /** El offset perceptual del doc 04 §6, en ms. Sale de la calibración por
        loopback y se persiste **por configuración de dispositivo**, porque
        depende de la latencia de la salida que se esté usando. Mientras no se
        calibre vale cero, y el informe lo dice en vez de fingir precisión. */
    double perceptualOffsetMs { 0.0 };

    /** %APPDATA%\Keyla\settings.json — el sitio normal en Windows. */
    static juce::File file();

    static Settings load();
    void save() const;
};

} // namespace keyla::app
