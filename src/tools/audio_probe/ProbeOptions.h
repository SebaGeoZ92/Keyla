#pragma once

#include <juce_core/juce_core.h>

namespace keyla::probe
{

enum class Mode
{
    listDevices,    // --list
    live,           // por defecto: tocas y mide
    selfTest,       // --selftest: genera el MIDI él mismo (loopMIDI)
    calibrate       // --calibrate: loopback físico (doc 04 §3)
};

struct Options
{
    Mode mode { Mode::live };

    // Índice ("2") o trozo del nombre ("SE49"). Vacío = el predeterminado.
    // Los índices bailan en cuanto enchufas un cacharro o creas un puerto
    // virtual, así que el nombre es lo estable (doc 01 §1.1).
    juce::String audioOutputSpec;
    juce::String audioInputSpec;    // sólo se abre en --calibrate
    juce::String midiInputSpec;
    juce::String midiOutputSpec;

    double sampleRate { 48000.0 };
    int bufferSize { 128 };
    bool exclusiveMode { true };    // WASAPI Exclusive por defecto (doc 03 §2)
    bool useAsio { false };         // sólo si se compiló con KEYLA_ENABLE_ASIO

    int maxVoices { 32 };
    int stressVoices { 0 };         // voces perpetuas para medir CPU a polifonía plena
    double stressGain { 0.0 };      // 0 = se calculan pero no suenan

    // Latencia de salida medida con el método B. Si es negativa se usa la que
    // declara el driver (método A) — que miente, pero es lo que hay.
    double outputLatencyMsOverride { -1.0 };

    double durationSeconds { 0.0 };     // 0 = hasta Ctrl-C
    double pulsePeriodMs { 125.0 };     // separación de notas en --selftest
    int calibrationPulses { 20 };

    bool json { false };
    bool quiet { false };           // sin línea de estado en vivo
    juce::String outputPath;        // --out: el informe lo escribe el programa

    juce::String errorMessage;      // no vacío ⇒ parseo fallido
    bool showHelp { false };
};

// Parsea argv. Nunca lanza: los errores salen en errorMessage.
Options parseCommandLine (int argc, char* argv[]);

// Resuelve un --audio-out / --midi-in contra la lista real de dispositivos.
// Acepta un índice o un trozo del nombre, sin distinguir mayúsculas. Devuelve
// -1 si `spec` está vacío (usa el predeterminado) o si hay error.
int resolveDeviceSpec (const juce::String& spec,
                       const juce::StringArray& names,
                       const juce::String& optionName,
                       juce::String& error);

juce::String usageText();

} // namespace keyla::probe
