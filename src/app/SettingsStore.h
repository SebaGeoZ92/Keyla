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

    core::InstrumentId instrument { core::InstrumentId::piano };
    float masterVolume { 0.8f };
    float reverbMix { 0.12f };
    int volumeController { 7 };

    /** %APPDATA%\Keyla\settings.json — el sitio normal en Windows. */
    static juce::File file();

    static Settings load();
    void save() const;
};

} // namespace keyla::app
