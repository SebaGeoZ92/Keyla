#include "ProbeOptions.h"
#include "Utf8.h"

namespace keyla::probe
{

juce::String usageText()
{
    return R"(audio_probe — spike de latencia de Keyla (fase 0)

Uso: audio_probe [opciones]

Modos
  --list                    Enumera dispositivos de audio y puertos MIDI, y sale.
  (por defecto)             Modo en vivo: toca y mide latencia, dropouts, CPU y jitter.
  --selftest                Modo automatizable: genera él mismo el MIDI por un puerto
                            virtual (loopMIDI) y verifica los criterios del doc 04 §8.
                            No necesita teclado. Código de salida 0 = pasa.
  --calibrate               Medición de latencia por loopback con cable físico
                            (doc 04 §3). Necesita entrada y salida de audio.

Dispositivos
  Aceptan un índice ("2") o un trozo del nombre ("SE49", "H510"), sin distinguir
  mayúsculas. Usa el nombre: los índices cambian en cuanto enchufas algo o creas
  un puerto virtual.

  --audio-out <id>          Dispositivo de salida.
  --audio-in <id>           Dispositivo de entrada (sólo --calibrate).
  --midi-in <id>            Puerto MIDI de entrada.
  --midi-out <id>           Puerto MIDI de salida (generador de --selftest).

Formato de audio
  --sample-rate <hz>        Por defecto 48000.
  --buffer <samples>        Por defecto 128.
  --shared                  WASAPI Shared en vez de Exclusive (modo tutorial).
  --asio                    Usar ASIO. Sólo si se compiló con KEYLA_ENABLE_ASIO=ON.

Síntesis y carga
  --voices <n>              Polifonía máxima. Por defecto 32.
  --stress <n>              Mantiene n voces sonando siempre, para medir la CPU a
                            polifonía plena (criterio del doc 04 §8).
  --stress-gain <g>         Ganancia de esas voces. Por defecto 0: se calculan pero
                            no se oyen.

Medición
  --output-latency-ms <x>   Latencia de salida medida con --calibrate. Si no se da,
                            se usa la que declara el driver, que suele mentir.
  --duration <s>            Duración. 0 = hasta Ctrl-C. El criterio de dropouts
                            exige 600.
  --pulse-ms <x>            Separación entre notas generadas en --selftest (125).
  --pulses <n>              Número de impulsos de --calibrate (20).

Salida
  --out <fichero>           Escribe el informe también a un fichero, en UTF-8.
                            Úsalo en vez de "> fichero": PowerShell redirige en
                            UTF-16 y se come los acentos.
  --json                    Informe final en JSON.
  --quiet                   Sin línea de estado en vivo; sólo el informe final.
  --help                    Esto.
)"_u8;
}

namespace
{
    bool needsValue (const juce::String& arg)
    {
        static const char* withValue[] = {
            "--audio-out", "--audio-in", "--midi-in", "--midi-out",
            "--sample-rate", "--buffer", "--voices", "--stress", "--stress-gain",
            "--output-latency-ms", "--duration", "--pulse-ms", "--pulses", "--out"
        };

        for (auto* v : withValue)
            if (arg == v)
                return true;

        return false;
    }
}

int resolveDeviceSpec (const juce::String& spec,
                       const juce::StringArray& names,
                       const juce::String& optionName,
                       juce::String& error)
{
    if (spec.isEmpty())
        return -1;

    if (spec.containsOnly ("0123456789"))
    {
        const auto index = spec.getIntValue();

        if (! juce::isPositiveAndBelow (index, names.size()))
        {
            error = optionName + " " + spec + " no existe: hay " + juce::String (names.size())
                  + ". Usa --list, o mejor un trozo del nombre: " + optionName + " SE49";
            return -1;
        }

        return index;
    }

    // El nombre exacto gana antes de mirar coincidencias parciales: "SE49" es
    // también un trozo de "MIDIIN2 (SE49)", y sin esto el nombre más natural
    // que escribiría cualquiera sería siempre ambiguo.
    for (int i = 0; i < names.size(); ++i)
        if (names[i].equalsIgnoreCase (spec))
            return i;

    juce::Array<int> matches;

    for (int i = 0; i < names.size(); ++i)
        if (names[i].containsIgnoreCase (spec))
            matches.add (i);

    if (matches.isEmpty())
    {
        error = optionName + " \"" + spec + "\": ningún dispositivo con ese nombre. Usa --list."_u8;
        return -1;
    }

    if (matches.size() > 1)
    {
        juce::StringArray listed;

        for (auto i : matches)
            listed.add ("[" + juce::String (i) + "] " + names[i]);

        error = optionName + " \"" + spec + "\" encaja con " + juce::String (matches.size())
              + ": " + listed.joinIntoString (", ") + ". Concreta más."_u8;
        return -1;
    }

    return matches[0];
}

Options parseCommandLine (int argc, char* argv[])
{
    Options o;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        juce::String value;

        if (needsValue (arg))
        {
            if (i + 1 >= argc)
            {
                o.errorMessage = "Falta el valor de " + arg;
                return o;
            }

            value = juce::String (argv[++i]);
        }

        if      (arg == "--help" || arg == "-h") o.showHelp = true;
        else if (arg == "--list")                o.mode = Mode::listDevices;
        else if (arg == "--selftest")            o.mode = Mode::selfTest;
        else if (arg == "--calibrate")           o.mode = Mode::calibrate;
        else if (arg == "--shared")              o.exclusiveMode = false;
        else if (arg == "--asio")                o.useAsio = true;
        else if (arg == "--json")                o.json = true;
        else if (arg == "--quiet")               o.quiet = true;
        else if (arg == "--audio-out")           o.audioOutputSpec = value;
        else if (arg == "--audio-in")            o.audioInputSpec = value;
        else if (arg == "--midi-in")             o.midiInputSpec = value;
        else if (arg == "--midi-out")            o.midiOutputSpec = value;
        else if (arg == "--sample-rate")         o.sampleRate = value.getDoubleValue();
        else if (arg == "--buffer")              o.bufferSize = value.getIntValue();
        else if (arg == "--voices")              o.maxVoices = value.getIntValue();
        else if (arg == "--stress")              o.stressVoices = value.getIntValue();
        else if (arg == "--stress-gain")         o.stressGain = value.getDoubleValue();
        else if (arg == "--output-latency-ms")   o.outputLatencyMsOverride = value.getDoubleValue();
        else if (arg == "--duration")            o.durationSeconds = value.getDoubleValue();
        else if (arg == "--pulse-ms")            o.pulsePeriodMs = value.getDoubleValue();
        else if (arg == "--pulses")              o.calibrationPulses = value.getIntValue();
        else if (arg == "--out")                 o.outputPath = value;
        else
        {
            o.errorMessage = "Argumento desconocido: " + arg;
            return o;
        }
    }

    // ── Validación y valores derivados ──────────────────────────────────────

    if (o.sampleRate < 8000.0 || o.sampleRate > 384000.0)
        o.errorMessage = "--sample-rate fuera de rango";

    if (o.bufferSize < 16 || o.bufferSize > 8192)
        o.errorMessage = "--buffer fuera de rango";

    if (o.maxVoices < 1 || o.maxVoices > 256)
        o.errorMessage = "--voices fuera de rango (1..256)";

    if (o.stressVoices < 0 || o.stressVoices > o.maxVoices)
        o.errorMessage = "--stress debe estar entre 0 y --voices";

    if (o.pulsePeriodMs < 10.0)
        o.errorMessage = "--pulse-ms demasiado corto (mínimo 10)"_u8;

    if (o.mode == Mode::selfTest)
    {
        // El selftest tiene que terminar solo: nunca espera a un humano.
        if (o.durationSeconds <= 0.0)
            o.durationSeconds = 20.0;

        // El criterio de CPU del doc 04 §8 se define a 32 voces. Si no se pide
        // otra cosa, el selftest lo mide.
        if (o.stressVoices == 0)
            o.stressVoices = juce::jmin (32, o.maxVoices);
    }

    return o;
}

} // namespace keyla::probe
