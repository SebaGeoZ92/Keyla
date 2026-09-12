#pragma once

#include "AudioDeviceHost.h"

#include <juce_audio_devices/juce_audio_devices.h>

namespace keyla::app
{

/** Abre el puerto MIDI y lo mantiene abierto.

    El puerto se recuerda **por nombre**, nunca por índice (doc 01 §1.1). Los
    índices se desplazan en cuanto enchufas cualquier cosa o aparece un puerto
    virtual, y entonces la aplicación escucha un dispositivo distinto sin decir
    nada. Eso ya pasó en la fase 0 y costó dos tiradas de medición tiradas a la
    basura.

    Reconecta solo: si desenchufas el teclado y lo vuelves a enchufar, vuelve a
    sonar sin tocar nada. El sondeo va en el message thread, nunca cerca del
    audio.
*/
class MidiInputHost final : private juce::Timer,
                            private juce::MidiInputCallback
{
public:
    explicit MidiInputHost (AudioDeviceHost& audioHost);
    ~MidiInputHost() override;

    static juce::StringArray availableInputs();

    /** Elige el puerto por nombre y empieza a vigilarlo. Un nombre vacío cierra
        y deja de vigilar. */
    void useDevice (const juce::String& deviceName);

    juce::String wantedDeviceName() const { return wantedName; }
    bool isConnected() const { return midiInput != nullptr; }

    /** Cambia cuando el puerto se conecta o se desconecta, para que la UI se
        entere sin sondear ella. Se llama en el message thread. */
    std::function<void()> onConnectionChanged;

    /** Cada nota que llega, con su instante de reloj de pared en segundos.

        **Se llama desde el hilo del driver MIDI.** Existe para grabar sesiones
        de escucha, que necesitan tus teclas en la misma escala de tiempo que la
        canción capturada. Quien lo use no puede tardar: lo que haga aquí retrasa
        la nota siguiente. Se fija una vez, antes de abrir ningún puerto. */
    std::function<void (int pitch, bool isOn, double seconds)> onNoteObserved;

private:
    void timerCallback() override;
    void handleIncomingMidiMessage (juce::MidiInput* source,
                                    const juce::MidiMessage& message) override;

    void tryConnect();
    void disconnect();

    AudioDeviceHost& host;
    juce::String wantedName;
    std::unique_ptr<juce::MidiInput> midiInput;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiInputHost)
};

} // namespace keyla::app
