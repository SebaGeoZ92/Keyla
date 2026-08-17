// audio_probe — spike de latencia de Keyla, fase 0 (doc 05).
//
// Cadena completa: teclado MIDI → FIFO lock-free → hilo de audio → sinusoide →
// WASAPI Exclusive → auriculares. Sin UI, sin samples, sin arquitectura.
// Su único trabajo es decir si este PC cumple los criterios del doc 04 §8.

#include "LoopbackCalibrator.h"
#include "MidiPulseGenerator.h"
#include "ProbeEngine.h"
#include "ProbeOptions.h"
#include "Report.h"
#include "Utf8.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <csignal>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>

#if JUCE_WINDOWS
 #include <windows.h>
 #include <io.h>
 #include <cstdio>
#endif

using namespace keyla::probe;

namespace
{

std::atomic<bool> gStopRequested { false };
std::atomic<bool> gReportWritten { false };

// Ctrl-C en mitad de una tirada larga no puede tirar a la basura lo medido
// hasta ese momento. Con signal() el proceso muere antes de que dé tiempo a
// escribir el informe; el manejador de consola de Windows sí puede retener la
// terminación mientras el hilo principal termina de escribirlo.
#if JUCE_WINDOWS
BOOL WINAPI consoleHandler (DWORD signal)
{
    if (signal != CTRL_C_EVENT && signal != CTRL_BREAK_EVENT && signal != CTRL_CLOSE_EVENT)
        return FALSE;

    gStopRequested.store (true, std::memory_order_release);

    // Windows da unos segundos antes de matar el proceso. Se aprovechan.
    for (int i = 0; i < 100 && ! gReportWritten.load (std::memory_order_acquire); ++i)
        std::this_thread::sleep_for (std::chrono::milliseconds (50));

    return TRUE;    // gestionado: no termines tú
}
#else
extern "C" void handleInterrupt (int)
{
    gStopRequested.store (true, std::memory_order_release);
}
#endif

void installInterruptHandler()
{
   #if JUCE_WINDOWS
    SetConsoleCtrlHandler (consoleHandler, TRUE);
   #else
    std::signal (SIGINT, handleInterrupt);
   #endif
}

void out (const juce::String& s)
{
    std::cout << s.toStdString() << '\n';
}

double nowSeconds() noexcept
{
    return juce::Time::getMillisecondCounterHiRes() * 0.001;
}

// Bluetooth son 100-300 ms y liquida el producto (doc 01 §2.1). Un receptor de
// 2,4 GHz es otra cosa: añade bastante menos y a veces el driver ni lo declara.
// Confundirlos y gritar "Bluetooth" ante unos auriculares gaming es un aviso que
// el usuario aprende a ignorar, así que se separan.
bool looksLikeBluetooth (const juce::String& name)
{
    static const char* hints[] = { "bluetooth", "bth", "airpods", "hands-free", "a2dp", "avrcp" };

    for (auto* h : hints)
        if (name.containsIgnoreCase (h))
            return true;

    return false;
}

bool looksWireless (const juce::String& name)
{
    static const char* hints[] = { "wireless", "inal", "2.4g", "dongle" };

    for (auto* h : hints)
        if (name.containsIgnoreCase (h))
            return true;

    return false;
}

// ── Tipo de dispositivo de audio ────────────────────────────────────────────

std::unique_ptr<juce::AudioIODeviceType> createDeviceType (const Options& o, juce::String& error)
{
   #if JUCE_WINDOWS
    if (o.useAsio)
    {
       #if JUCE_ASIO
        return std::unique_ptr<juce::AudioIODeviceType> (juce::AudioIODeviceType::createAudioIODeviceType_ASIO());
       #else
        error = "Este binario se compiló sin ASIO. Reconfigura con -DKEYLA_ENABLE_ASIO=ON "
                "y -DKEYLA_ASIO_SDK_PATH=<ruta>/common (doc 03 §2)."_u8;
        return {};
       #endif
    }

    const auto mode = o.exclusiveMode ? juce::WASAPIDeviceMode::exclusive
                                      : juce::WASAPIDeviceMode::shared;

    return std::unique_ptr<juce::AudioIODeviceType> (juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (mode));
   #else
    juce::ignoreUnused (o);
    error = "audio_probe sólo está portado a Windows por ahora."_u8;
    return {};
   #endif
}

// ── Enumeración ─────────────────────────────────────────────────────────────

int runList (const Options& o)
{
    juce::String error;
    auto type = createDeviceType (o, error);

    if (type == nullptr)
    {
        out ("ERROR: " + error);
        return 2;
    }

    type->scanForDevices();

    out ("");
    out ("Backend de audio: " + type->getTypeName());
    out ("");
    out ("  Salidas de audio  (--audio-out <n>)");

    const auto outputs = type->getDeviceNames (false);
    const auto defOut = type->getDefaultDeviceIndex (false);

    for (int i = 0; i < outputs.size(); ++i)
        out ("    [" + juce::String (i) + "] " + outputs[i] + (i == defOut ? "   (predeterminado)" : ""));

    if (outputs.isEmpty())
        out ("    (ninguna)");

    out ("");
    out ("  Entradas de audio  (--audio-in <n>, sólo para --calibrate)"_u8);

    const auto inputs = type->getDeviceNames (true);
    const auto defIn = type->getDefaultDeviceIndex (true);

    for (int i = 0; i < inputs.size(); ++i)
        out ("    [" + juce::String (i) + "] " + inputs[i] + (i == defIn ? "   (predeterminada)" : ""));

    if (inputs.isEmpty())
        out ("    (ninguna)");

    out ("");
    out ("  Entradas MIDI  (--midi-in <n>)");

    const auto midiIns = juce::MidiInput::getAvailableDevices();

    for (int i = 0; i < midiIns.size(); ++i)
        out ("    [" + juce::String (i) + "] " + midiIns[i].name);

    if (midiIns.isEmpty())
        out ("    (ninguna)");

    out ("");
    out ("  Salidas MIDI  (--midi-out <n>)");

    const auto midiOuts = juce::MidiOutput::getAvailableDevices();

    for (int i = 0; i < midiOuts.size(); ++i)
        out ("    [" + juce::String (i) + "] " + midiOuts[i].name);

    if (midiOuts.isEmpty())
        out ("    (ninguna)");

    out ("");
    return 0;
}

// ── Apertura del dispositivo de audio ───────────────────────────────────────

struct OpenedDevice
{
    std::unique_ptr<juce::AudioIODeviceType> type;
    std::unique_ptr<juce::AudioIODevice> device;
    juce::String error;
    juce::String warning;
};

OpenedDevice openAudioDevice (const Options& o, bool wantInput)
{
    OpenedDevice r;

    r.type = createDeviceType (o, r.error);

    if (r.type == nullptr)
        return r;

    r.type->scanForDevices();

    const auto outputs = r.type->getDeviceNames (false);

    if (outputs.isEmpty())
    {
        r.error = "No hay dispositivos de salida en " + r.type->getTypeName() + ".";
        return r;
    }

    int outIdx = resolveDeviceSpec (o.audioOutputSpec, outputs, "--audio-out", r.error);

    if (r.error.isNotEmpty())
        return r;

    if (outIdx < 0)
        outIdx = r.type->getDefaultDeviceIndex (false);

    if (! juce::isPositiveAndBelow (outIdx, outputs.size()))
    {
        r.error = "No hay una salida de audio predeterminada utilizable. Elige una con --audio-out (--list).";
        return r;
    }

    juce::String inName;

    if (wantInput)
    {
        const auto inputs = r.type->getDeviceNames (true);

        if (inputs.isEmpty())
        {
            r.error = "No hay dispositivos de entrada: --calibrate necesita uno.";
            return r;
        }

        int inIdx = resolveDeviceSpec (o.audioInputSpec, inputs, "--audio-in", r.error);

        if (r.error.isNotEmpty())
            return r;

        if (inIdx < 0)
            inIdx = r.type->getDefaultDeviceIndex (true);

        if (! juce::isPositiveAndBelow (inIdx, inputs.size()))
        {
            r.error = "No hay una entrada de audio predeterminada utilizable. Elige una con --audio-in (--list).";
            return r;
        }

        inName = inputs[inIdx];
    }

    r.device.reset (r.type->createDevice (outputs[outIdx], inName));

    if (r.device == nullptr)
    {
        r.error = "No se pudo crear el dispositivo \"" + outputs[outIdx] + "\".";
        return r;
    }

    // ── Sample rate ─────────────────────────────────────────────────────────
    const auto rates = r.device->getAvailableSampleRates();

    if (! rates.contains (o.sampleRate))
    {
        juce::StringArray listed;

        for (auto rate : rates)
            listed.add (juce::String (rate, 0));

        r.error = "El dispositivo no admite " + juce::String (o.sampleRate, 0)
                + " Hz en este modo. Admite: " + listed.joinIntoString (", ") + ".";
        return r;
    }

    // ── Tamaño de buffer ────────────────────────────────────────────────────
    const auto sizes = r.device->getAvailableBufferSizes();
    int bufferSize = o.bufferSize;

    if (! sizes.contains (bufferSize))
    {
        int best = sizes.isEmpty() ? bufferSize : sizes[0];

        for (auto s : sizes)
            if (std::abs (s - o.bufferSize) < std::abs (best - o.bufferSize))
                best = s;

        juce::StringArray listed;

        for (auto s : sizes)
            listed.add (juce::String (s));

        r.warning = "El dispositivo no admite un buffer de " + juce::String (o.bufferSize)
                  + "; se usa " + juce::String (best) + ". Admite: " + listed.joinIntoString (", ") + ".";
        bufferSize = best;
    }

    // ── Canales ─────────────────────────────────────────────────────────────
    const auto numOutChannels = r.device->getOutputChannelNames().size();
    const auto numInChannels = r.device->getInputChannelNames().size();

    juce::BigInteger outChannels;
    outChannels.setRange (0, juce::jmin (2, numOutChannels), true);

    juce::BigInteger inChannels;

    if (wantInput)
        inChannels.setRange (0, juce::jmin (2, numInChannels), true);

    auto err = r.device->open (inChannels, outChannels, o.sampleRate, bufferSize);

    if (err.isNotEmpty())
    {
        // WASAPI Exclusive suele exigir el formato nativo del dispositivo:
        // segundo intento con todos los canales.
        outChannels.clear();
        outChannels.setRange (0, numOutChannels, true);

        if (wantInput)
        {
            inChannels.clear();
            inChannels.setRange (0, numInChannels, true);
        }

        const auto err2 = r.device->open (inChannels, outChannels, o.sampleRate, bufferSize);

        if (err2.isNotEmpty())
        {
            r.error = "No se pudo abrir el dispositivo: " + err
                    + (err2 == err ? juce::String() : " / " + err2)
                    + (o.exclusiveMode ? "\n  En modo exclusivo esto suele significar que otra "
                                         "aplicación tiene el dispositivo tomado, o que el formato "
                                         "no coincide con el configurado en Windows. Prueba --shared."_u8
                                       : juce::String());
            r.device.reset();
            return r;
        }
    }

    // Algunos drivers virtuales devuelven cadena vacía de open() y se quedan sin
    // abrir. La cadena de error no basta: isOpen() exige además que el hilo del
    // dispositivo esté vivo, que es lo que de verdad queremos saber.
    if (! r.device->isOpen() || r.device->getCurrentSampleRate() <= 0.0)
    {
        const auto lastError = r.device->getLastError();

        r.error = "El dispositivo aceptó abrirse pero no está abierto: "_u8
                + juce::String (r.device->getCurrentSampleRate(), 0) + " Hz, buffer "
                + juce::String (r.device->getCurrentBufferSizeSamples()) + " samples."
                + (lastError.isEmpty() ? juce::String() : "\n  El driver dice: " + lastError)
                + "\n  Es típico de dispositivos virtuales en modo exclusivo. Elige la salida\n"
                  "  física con --audio-out (--list) o usa --shared."_u8;
        r.device.reset();
        return r;
    }

    return r;
}

// ── Resolución de puertos MIDI ──────────────────────────────────────────────

// Que un puerto de entrada y uno de salida se llamen igual NO significa que
// formen un bucle: el SE49 aparece en las dos listas y lo que le mandes no
// vuelve. La única comprobación fiable es empírica — mandar algo y esperarlo.
class MidiLoopbackProbe final : public juce::MidiInputCallback
{
public:
    static constexpr int channel = 16;
    static constexpr int pitch = 0;

    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
    {
        if (m.isNoteOn() && m.getChannel() == channel && m.getNoteNumber() == pitch)
            seen.store (true, std::memory_order_release);
    }

    bool sawIt() const noexcept { return seen.load (std::memory_order_acquire); }

private:
    std::atomic<bool> seen { false };
};

bool midiPortsFormALoop (const juce::MidiDeviceInfo& in, const juce::MidiDeviceInfo& outPort)
{
    MidiLoopbackProbe probe;

    auto midiIn = juce::MidiInput::openDevice (in.identifier, &probe);
    auto midiOut = juce::MidiOutput::openDevice (outPort.identifier);

    if (midiIn == nullptr || midiOut == nullptr)
        return false;

    midiIn->start();

    // Nota inaudible en el canal 16: inerte incluso si al otro lado hay un
    // teclado de verdad escuchando.
    midiOut->sendMessageNow (juce::MidiMessage::noteOn (MidiLoopbackProbe::channel,
                                                        MidiLoopbackProbe::pitch,
                                                        static_cast<juce::uint8> (1)));

    const double deadline = nowSeconds() + 0.3;

    while (! probe.sawIt() && nowSeconds() < deadline)
        std::this_thread::sleep_for (std::chrono::milliseconds (5));

    midiOut->sendMessageNow (juce::MidiMessage::noteOff (MidiLoopbackProbe::channel,
                                                         MidiLoopbackProbe::pitch));
    midiIn->stop();

    return probe.sawIt();
}

struct MidiSelection
{
    juce::MidiDeviceInfo input;
    juce::MidiDeviceInfo output;
    bool hasInput { false };
    bool hasOutput { false };
    juce::String error;
    juce::String warning;
    juce::StringArray allInputNames;    // para poder sugerir alternativas
};

MidiSelection resolveMidiPorts (const Options& o)
{
    MidiSelection sel;

    const auto ins = juce::MidiInput::getAvailableDevices();
    const auto outs = juce::MidiOutput::getAvailableDevices();

    juce::StringArray inNames, outNames;

    for (const auto& d : ins)  inNames.add (d.name);
    for (const auto& d : outs) outNames.add (d.name);

    sel.allInputNames = inNames;

    const int inIdx = resolveDeviceSpec (o.midiInputSpec, inNames, "--midi-in", sel.error);

    if (sel.error.isNotEmpty())
        return sel;

    if (inIdx >= 0)
    {
        sel.input = ins[inIdx];
        sel.hasInput = true;
    }

    const int outIdx = resolveDeviceSpec (o.midiOutputSpec, outNames, "--midi-out", sel.error);

    if (sel.error.isNotEmpty())
        return sel;

    if (outIdx >= 0)
    {
        sel.output = outs[outIdx];
        sel.hasOutput = true;
    }

    if (o.mode == Mode::selfTest)
    {
        if (sel.hasInput && sel.hasOutput)
        {
            // Elegidos a mano: se comprueban igual. Un par que no hace bucle da
            // cero notas y un informe incomprensible.
            if (! midiPortsFormALoop (sel.input, sel.output))
            {
                sel.error = "Lo que mando por \"" + sel.output.name + "\" no vuelve por \""
                          + sel.input.name + "\": ese par no forma un bucle.\n"
                            "  --selftest necesita un puerto virtual de loopMIDI."_u8;
                return sel;
            }

            return sel;
        }

        // Candidatos: mismo nombre a los dos lados, los de loopMIDI primero. El
        // nombre sólo sirve para ordenar la búsqueda; quien decide es el bucle.
        std::vector<std::pair<juce::MidiDeviceInfo, juce::MidiDeviceInfo>> candidates;

        for (const auto& i : ins)
            for (const auto& oDev : outs)
                if (i.name.equalsIgnoreCase (oDev.name))
                    candidates.emplace_back (i, oDev);

        std::stable_partition (candidates.begin(), candidates.end(),
                               [] (const auto& p) { return p.first.name.containsIgnoreCase ("loopmidi"); });

        for (const auto& [candidateIn, candidateOut] : candidates)
        {
            if (midiPortsFormALoop (candidateIn, candidateOut))
            {
                sel.input = candidateIn;
                sel.output = candidateOut;
                sel.hasInput = true;
                sel.hasOutput = true;
                return sel;
            }
        }

        sel.error = candidates.empty()
                  ? "--selftest necesita un puerto MIDI virtual: una entrada y una salida con\n"
                    "  el mismo nombre que devuelva lo que se le manda. No hay ninguna pareja.\n"
                    "  Instala loopMIDI, crea un puerto y vuelve a ejecutar."_u8
                  : "Hay parejas de puertos con el mismo nombre, pero ninguna devuelve lo que\n"
                    "  se le manda (un teclado aparece en las dos listas y no hace bucle).\n"
                    "  Instala loopMIDI y crea un puerto virtual."_u8;
        return sel;
    }
    else if (! sel.hasInput)
    {
        // Modo en vivo: si sólo hay un puerto, se coge sin preguntar (doc 01 §1.1).
        if (ins.size() == 1)
        {
            sel.input = ins[0];
            sel.hasInput = true;
        }
        else if (ins.size() > 1)
        {
            // Ambiguo, pero no es motivo para no arrancar: medir la estabilidad
            // del audio no necesita MIDI. Se avisa y se sigue sin él; el informe
            // dirá SIN DATOS en la latencia, que es la verdad.
            juce::StringArray names;

            for (int i = 0; i < ins.size(); ++i)
                names.add ("[" + juce::String (i) + "] " + ins[i].name);

            sel.warning = "hay " + juce::String (ins.size()) + " entradas MIDI y no has elegido ninguna,\n"
                          "  así que no se abre MIDI y no habrá latencia. Elige con --midi-in <n>:\n    "_u8
                        + names.joinIntoString ("\n    ");
        }
    }

    return sel;
}

// ── Bucle de medición ───────────────────────────────────────────────────────

struct Collected
{
    Percentiles latency;
    Percentiles bufferWait;
    Percentiles cpu;
    std::vector<NoteRecord> arrivals;
    std::vector<PulseRecord> pulses;
};

void drain (ProbeEngine& engine, MidiPulseGenerator* generator, Collected& c)
{
    NoteRecord n {};

    while (engine.popNote (n))
    {
        c.latency.add (n.systemLatencyMs);
        c.bufferWait.add (n.bufferWaitMs);
        c.arrivals.push_back (n);
    }

    LoadRecord l {};

    while (engine.popLoad (l))
        c.cpu.add (static_cast<double> (l.cpuFraction));

    if (generator != nullptr)
    {
        PulseRecord p {};

        while (generator->popPulse (p))
            c.pulses.push_back (p);
    }
}

// Con la salida redirigida a un fichero el \r no borra nada y el log sale
// ilegible: ahí se imprime una línea suelta de vez en cuando.
bool stdoutIsConsole()
{
   #if JUCE_WINDOWS
    return _isatty (_fileno (stdout)) != 0;
   #else
    return true;
   #endif
}

void printStatus (const ProbeSnapshot& s, Collected& c, double elapsed, double totalSeconds, bool console)
{
    // En consola, una sola línea que se reescribe sobre sí misma. Se mantiene
    // por debajo de 100 columnas: si envuelve, el \r la deja hecha un desastre.
    juce::String l;
    l << (console ? "\r  " : "  ") << juce::String (elapsed, 1);

    if (totalSeconds > 0.0)
        l << "/" << juce::String (totalSeconds, 0);

    l << "s  notas " << juce::String (s.noteOnCount);

    if (! c.latency.empty())
        l << "  lat p50 " << juce::String (c.latency.value (0.50), 2)
          << " max " << juce::String (c.latency.maximum(), 2);

    l << "  drop " << juce::String (s.gapDropouts)
      << "  cpu " << juce::String (s.cpuMean * 100.0, 1) << "/" << juce::String (s.cpuMax * 100.0, 1) << "%"
      << "  jit " << juce::String (s.callbackDeltaSigma, 3);

    if (console)
        std::cout << l.paddedRight (' ', 98).toStdString() << std::flush;
    else
        std::cout << l.toStdString() << '\n' << std::flush;
}

// ── Modo principal (en vivo y selftest) ─────────────────────────────────────

int runProbe (const Options& o)
{
    auto opened = openAudioDevice (o, false);

    if (opened.device == nullptr)
    {
        out ("ERROR: " + opened.error);
        return 2;
    }

    if (opened.warning.isNotEmpty())
        out ("AVISO: " + opened.warning);

    auto midi = resolveMidiPorts (o);

    if (midi.error.isNotEmpty())
    {
        out ("ERROR: " + midi.error);
        return 2;
    }

    if (midi.warning.isNotEmpty())
        out ("AVISO: " + midi.warning);
    else if (! midi.hasInput)
        out ("AVISO: no hay ninguna entrada MIDI abierta; no se medirá latencia."_u8);

    auto engine = std::make_unique<ProbeEngine> (o.maxVoices, o.stressVoices, o.stressGain);
    engine->setOutputLatencyOverrideMs (o.outputLatencyMsOverride);

    auto* device = opened.device.get();

    std::unique_ptr<juce::MidiInput> midiIn;

    if (midi.hasInput)
    {
        midiIn = juce::MidiInput::openDevice (midi.input.identifier, engine.get());

        if (midiIn == nullptr)
        {
            out ("ERROR: no se pudo abrir la entrada MIDI \"" + midi.input.name + "\".");
            return 2;
        }
    }

    std::unique_ptr<MidiPulseGenerator> generator;

    if (o.mode == Mode::selfTest && midi.hasOutput)
    {
        auto midiOut = juce::MidiOutput::openDevice (midi.output.identifier);

        if (midiOut == nullptr)
        {
            out ("ERROR: no se pudo abrir la salida MIDI \"" + midi.output.name + "\".");
            return 2;
        }

        generator = std::make_unique<MidiPulseGenerator> (std::move (midiOut), o.pulsePeriodMs);
    }

    // ── Arranque ────────────────────────────────────────────────────────────
    device->start (engine.get());

    // Abrirse y arrancar son cosas distintas: hay drivers virtuales que aceptan
    // open(), levantan el flag y sueltan el stream acto seguido. Si en segundo y
    // medio no ha entrado ni un callback, no hay nada que medir. Se comprueba
    // antes de anunciar el formato, que hasta aquí no es de fiar.
    {
        const double deadline = nowSeconds() + 1.5;

        while (engine->snapshot().callbacks == 0 && nowSeconds() < deadline)
            std::this_thread::sleep_for (std::chrono::milliseconds (25));

        if (engine->snapshot().callbacks == 0)
        {
            const auto lastError = device->getLastError();

            device->stop();
            out ("");
            out ("ERROR: \"" + device->getName() + "\" se abrió pero no ha producido ni un solo\n"
                 "  callback de audio: el stream no ha arrancado."_u8
                 + (lastError.isEmpty() ? juce::String() : "\n  El driver dice: " + lastError)
                 + (o.exclusiveMode
                        ? "\n  Los dispositivos virtuales suelen hacer esto en modo exclusivo. Elige\n"
                          "  la salida física con --audio-out (--list) o prueba --shared."_u8
                        : "\n  Elige otra salida con --audio-out (--list): ésta no da un stream ni en\n"
                          "  modo compartido."_u8));
            return 2;
        }
    }

    out ("");
    out ("  Dispositivo : " + device->getName() + "   [" + opened.type->getTypeName() + "]");
    out ("  Formato     : " + juce::String (device->getCurrentSampleRate(), 0) + " Hz  ·  buffer "_u8
         + juce::String (device->getCurrentBufferSizeSamples()) + " samples");
    out ("  MIDI in     : " + (midi.hasInput ? midi.input.name : juce::String ("(ninguna)")));

    if (generator != nullptr)
        out ("  MIDI out    : " + midi.output.name + "   (generador del selftest)");

    if (o.stressVoices > 0)
        out ("  Estrés      : "_u8 + juce::String (o.stressVoices) + " voces permanentes, ganancia "
             + juce::String (o.stressGain, 2));

    out ("");

    // Un puerto virtual en modo en vivo no trae nada: por ahí no toca nadie.
    // Es exactamente el error que se comete al copiar un comando con --midi-in 0
    // después de haber creado un puerto de loopMIDI, que desplaza los índices.
    if (o.mode == Mode::live && midi.hasInput && midi.input.name.containsIgnoreCase ("loopmidi"))
    {
        out ("  *** AVISO: \"" + midi.input.name + "\" es un puerto virtual: por ahí no llega"_u8);
        out ("      ningún teclado. Si querías tocar, elige la entrada por nombre:"_u8);
        out ("          --midi-in SE49");
        out ("");
    }

    if (o.mode == Mode::live)
        out ("  Toca. Ctrl-C para terminar y ver el informe.");
    else
        out ("  Selftest en marcha, " + juce::String (o.durationSeconds, 0) + " s. No hace falta tocar nada.");

    out ("");

    if (midiIn != nullptr)
        midiIn->start();

    if (generator != nullptr)
        generator->start();

    // ── Bucle de la consola: polling, nunca notificaciones (invariante 5) ───
    Collected collected;
    collected.latency.reserve (4096);
    collected.cpu.reserve (262144);

    const bool console = stdoutIsConsole();
    const double statusPeriod = console ? 0.5 : 5.0;

    const double startTime = nowSeconds();
    double lastStatus = 0.0;
    bool silenceWarned = false;

    for (;;)
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (20));

        drain (*engine, generator.get(), collected);

        const double elapsed = nowSeconds() - startTime;

        // Diez minutos midiendo un puerto por el que no llega nada es diez
        // minutos tirados. Si nadie ha tocado, hay que decirlo cuanto antes.
        if (! silenceWarned && midi.hasInput && elapsed > 10.0
            && engine->snapshot().noteOnCount == 0)
        {
            silenceWarned = true;

            if (console && ! o.quiet)
                std::cout << '\n';

            out ("  *** AVISO: 10 s sin recibir una sola nota por \"" + midi.input.name + "\"."_u8);

            juce::StringArray others;

            for (const auto& n : midi.allInputNames)
                if (n != midi.input.name)
                    others.add ("--midi-in \"" + n + "\"");

            if (! others.isEmpty())
                out ("      Si estás tocando, el puerto no es ése. Prueba: "_u8 + others.joinIntoString ("  o  "));
            else
                out ("      ¿Está encendido el teclado?"_u8);

            out ("");
        }

        if (! o.quiet && elapsed - lastStatus >= statusPeriod)
        {
            printStatus (engine->snapshot(), collected, elapsed, o.durationSeconds, console);
            lastStatus = elapsed;
        }

        if (gStopRequested.load (std::memory_order_acquire))
            break;

        if (o.durationSeconds > 0.0 && elapsed >= o.durationSeconds)
            break;
    }

    if (! o.quiet && console)
        std::cout << '\n';

    // ── Parada ordenada ─────────────────────────────────────────────────────
    if (generator != nullptr)
        generator->stop();

    if (midiIn != nullptr)
        midiIn->stop();

    const int xruns = device->getXRunCount();

    device->stop();
    drain (*engine, generator.get(), collected);

    const auto snap = engine->snapshot();

    // ── Composición del informe ─────────────────────────────────────────────
    ProbeResults r;
    r.mode = o.mode == Mode::selfTest ? "selftest" : "en vivo";
    r.audioDeviceType = opened.type->getTypeName();
    r.audioDeviceName = device->getName();
    r.midiInputName = midi.hasInput ? midi.input.name : juce::String();
    r.midiOutputName = generator != nullptr ? midi.output.name : juce::String();
    r.sampleRate = device->getCurrentSampleRate();
    r.bufferSize = device->getCurrentBufferSizeSamples();
    r.exclusive = o.exclusiveMode;
    r.bluetoothSuspected = looksLikeBluetooth (device->getName());
    r.wirelessSuspected = ! r.bluetoothSuspected && looksWireless (device->getName());
   #if JUCE_DEBUG
    r.debugBuild = true;
   #endif

    r.driverOutputLatencyMs = 1000.0 * device->getOutputLatencyInSamples() / r.sampleRate;
    r.usedOutputLatencyMs = engine->outputLatencySeconds() * 1000.0;
    r.outputLatencyMeasured = o.outputLatencyMsOverride >= 0.0;

    r.runSeconds = nowSeconds() - startTime;
    r.requestedSeconds = o.durationSeconds;
    r.interrupted = o.durationSeconds > 0.0 && r.runSeconds < o.durationSeconds - 0.5;
    r.noteOnCount = snap.noteOnCount;
    r.callbacks = snap.callbacks;

    r.hasLatency = ! collected.latency.empty();

    if (r.hasLatency)
    {
        r.latencyP50 = collected.latency.value (0.50);
        r.latencyP95 = collected.latency.value (0.95);
        r.latencyMax = collected.latency.maximum();
        r.bufferWaitP50 = collected.bufferWait.value (0.50);
        r.bufferWaitMax = collected.bufferWait.maximum();
    }

    r.gapDropouts = snap.gapDropouts;
    r.driverXRuns = xruns;
    r.midiQueueDrops = engine->midiQueueDrops();
    r.recordQueueDrops = engine->recordQueueDrops();
    r.lateEvents = snap.lateEvents;

    r.hasCpu = ! collected.cpu.empty();
    r.cpuMean = collected.cpu.mean();
    r.cpuP95 = collected.cpu.value (0.95);
    r.cpuMax = collected.cpu.maximum();
    r.stressVoices = o.stressVoices;

    r.callbackPeriodMs = 1000.0 * r.bufferSize / r.sampleRate;
    r.callbackSigmaMs = snap.callbackDeltaSigma;
    r.callbackMaxMs = snap.callbackDeltaMax;

    // ── Jitter de entrada MIDI ──────────────────────────────────────────────
    if (generator != nullptr && ! collected.pulses.empty() && ! collected.arrivals.empty())
    {
        // Se emparejan en orden y se comprueba la altura, que va cambiando: si
        // se perdiera un mensaje lo veríamos aquí en vez de medir basura.
        RunningStats transport;
        std::size_t pi = 0;
        int matched = 0;

        for (const auto& a : collected.arrivals)
        {
            while (pi < collected.pulses.size() && collected.pulses[pi].pitch != a.pitch)
                ++pi;

            if (pi >= collected.pulses.size())
                break;

            transport.add ((a.arrivalHostSeconds - collected.pulses[pi].actualSeconds) * 1000.0);
            ++matched;
            ++pi;
        }

        if (matched >= 8)
        {
            r.hasMidiJitter = true;
            r.midiJitterSigmaMs = transport.stddev();
            r.midiTransportMeanMs = transport.mean;
            r.midiTransportMaxMs = transport.maximum;
            r.midiJitterSamples = matched;
            r.midiJitterSource = "puerto virtual: mide la ruta software, no el bus USB del teclado";
        }
    }
    else if (collected.arrivals.size() >= 8)
    {
        // Con un teclado físico no sabemos cuándo tocaba llegar cada nota, así
        // que esto incluye tu propio timing. Es informativo, no es el criterio.
        RunningStats inter;

        for (std::size_t i = 1; i < collected.arrivals.size(); ++i)
            inter.add ((collected.arrivals[i].arrivalHostSeconds
                        - collected.arrivals[i - 1].arrivalHostSeconds) * 1000.0);

        r.midiJitterSigmaMs = inter.stddev();
        r.midiJitterSamples = static_cast<int> (inter.n);
        r.midiJitterSource = "sigma de intervalos entre ataques: incluye tu propio timing";
        // hasMidiJitter se queda en false a propósito: no sirve como criterio.
    }

    const auto criteria = evaluateCriteria (r);

    std::ostringstream report;

    if (o.json)
        printJsonReport (report, r, criteria);
    else
        printTextReport (report, r, criteria);

    std::cout << report.str() << std::flush;

    // El fichero lo escribe el programa, no la shell: PowerShell redirige en
    // UTF-16 y destroza los acentos.
    if (o.outputPath.isNotEmpty())
    {
        const juce::File target (juce::File::getCurrentWorkingDirectory()
                                     .getChildFile (o.outputPath));

        if (target.replaceWithText (juce::String::fromUTF8 (report.str().c_str()), false, false, "\r\n"))
            out ("  [informe escrito en " + target.getFullPathName() + "]");
        else
            out ("  [AVISO: no se pudo escribir " + target.getFullPathName() + "]");
    }

    gReportWritten.store (true, std::memory_order_release);

    // Sólo el selftest tiene código de salida estricto: el modo en vivo es una
    // herramienta interactiva, no un test.
    return o.mode == Mode::selfTest ? exitCodeFor (criteria) : 0;
}

// ── Calibración por loopback ────────────────────────────────────────────────

int runCalibrate (const Options& o)
{
    auto opened = openAudioDevice (o, true);

    if (opened.device == nullptr)
    {
        out ("ERROR: " + opened.error);
        return 2;
    }

    if (opened.warning.isNotEmpty())
        out ("AVISO: " + opened.warning);

    auto* device = opened.device.get();

    LoopbackCalibrator calibrator (o.calibrationPulses, 0.5);

    out ("");
    out ("  Calibración por loopback (doc 04 §3)"_u8);
    out ("  Dispositivo : " + device->getName());
    out ("  Formato     : " + juce::String (device->getCurrentSampleRate(), 0) + " Hz  ·  buffer "_u8
         + juce::String (device->getCurrentBufferSizeSamples()));
    out ("");
    out ("  Conecta un cable de la salida de auriculares a la entrada de línea/micro."_u8);
    out ("  Se emiten " + juce::String (o.calibrationPulses) + " chirps y se busca su eco.");
    out ("");

    device->start (&calibrator);

    const bool console = stdoutIsConsole();
    int lastShown = -1;

    while (! calibrator.isFinished() && ! gStopRequested.load (std::memory_order_acquire))
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (100));

        const int percent = static_cast<int> (calibrator.progress() * 100.0);

        if (console)
            std::cout << "\r  progreso " << percent << " %   " << std::flush;
        else if (percent >= lastShown + 25)
            std::cout << "  progreso " << (lastShown = percent / 25 * 25) << " %\n" << std::flush;
    }

    if (console)
        std::cout << '\n';
    device->stop();

    const auto result = calibrator.analyse();

    out ("");

    if (! result.ok)
    {
        out ("  FALLO: " + result.message);
        out ("  (pico/ruido peor caso: " + juce::String (result.peakToNoise, 1) + ")");
        out ("");
        return 1;
    }

    const double rtMs = 1000.0 * result.medianSamples / result.sampleRate;
    const double sigmaMs = 1000.0 * result.sigmaSamples / result.sampleRate;

    out ("  Repeticiones detectadas   " + juce::String (result.detected) + " / " + juce::String (result.attempted));
    out ("  Round-trip (mediana)      " + juce::String (result.medianSamples, 1) + " samples  = "
         + juce::String (rtMs, 2) + " ms");
    out ("  Dispersión (sigma)        "_u8 + juce::String (sigmaMs, 3) + " ms");
    out ("");

    if (sigmaMs > 1.0)
        out ("  AVISO: dispersión alta. El sistema tiene un problema de estabilidad,"
             "\n         no de latencia (doc 04 §3)."_u8);

    const double outputMs = rtMs * 0.5;

    out ("  Latencia de salida estimada  " + juce::String (outputMs, 2) + " ms   (round-trip / 2)");
    out ("  Declarada por el driver      "
         + juce::String (1000.0 * device->getOutputLatencyInSamples() / result.sampleRate, 2) + " ms");
    out ("");
    out ("  Vuelve a lanzar la medida en vivo con:");
    out ("      audio_probe --output-latency-ms " + juce::String (outputMs, 2));
    out ("");

    return 0;
}

} // namespace

int main (int argc, char* argv[])
{
   #if JUCE_WINDOWS
    SetConsoleOutputCP (CP_UTF8);
   #endif

    installInterruptHandler();

    const auto options = parseCommandLine (argc, argv);

    if (options.showHelp)
    {
        std::cout << usageText().toStdString();
        return 0;
    }

    if (options.errorMessage.isNotEmpty())
    {
        out ("ERROR: " + options.errorMessage);
        out ("");
        std::cout << usageText().toStdString();
        return 2;
    }

    const juce::ScopedJuceInitialiser_GUI juceInit;

    switch (options.mode)
    {
        case Mode::listDevices: return runList (options);
        case Mode::calibrate:   return runCalibrate (options);
        case Mode::live:
        case Mode::selfTest:    return runProbe (options);
    }

    return 0;
}
