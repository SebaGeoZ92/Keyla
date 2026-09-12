#include "ListeningEngine.h"
#include "StartupShortcut.h"
#include "ui/MainComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace keyla::app
{

class KeylaApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Keyla"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String& commandLine) override
    {
        // Si el acceso directo de Inicio quedó apuntando a un ejecutable
        // antiguo, se corrige aquí. Recompilar en otra carpeta rompería el
        // arranque automático sin avisar de nada.
        startup::refreshTargetIfEnabled();

        // Encender y apagar el arranque automático sin abrir la ventana. Es lo
        // mismo que hace la casilla, pero automatizable: sirve para un acceso
        // directo, para un script y —sobre todo— para poder comprobar que el
        // acceso directo se escribe de verdad, que es la parte de esto que no
        // se puede verificar mirando la pantalla.
        if (commandLine.contains ("--startup-on") || commandLine.contains ("--startup-off"))
        {
            juce::String error;
            startup::setEnabled (commandLine.contains ("--startup-on"), error);
            setApplicationReturnValue (error.isEmpty() ? 0 : 1);
            quit();
            return;
        }

        // Comprobar la escucha sin ventana y sin humano. Es la única forma de
        // saber si la captura de WASAPI recibe algo de verdad: lo demás que se
        // puede decir de ella es que compila.
        if (commandLine.contains ("--listen-test"))
        {
            runListenTest (commandLine);
            quit();
            return;
        }

        // Ojo con el orden: `--startup-on` contiene `--startup`, así que esto
        // tiene que ir después del bloque de arriba.
        const auto unattended = commandLine.contains (startup::unattendedFlag);

        mainWindow = std::make_unique<MainWindow> (unattended);
    }

    void shutdown() override
    {
        mainWindow.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    /** Doble clic en el icono con Keyla ya abierta y minimizada por el arranque
        automático. Sin esto no pasaría absolutamente nada y parecería rota. */
    void anotherInstanceStarted (const juce::String&) override
    {
        if (mainWindow != nullptr)
            mainWindow->wake();
    }

private:
    /** Escucha la salida predeterminada durante unos segundos y escribe lo que
        haya oído. Sin ventana: sirve para verificar la captura desde un script. */
    static void runListenTest (const juce::String& commandLine)
    {
        const auto seconds = commandLine.contains ("--seconds")
                           ? commandLine.fromFirstOccurrenceOf ("--seconds", false, false)
                                        .trim().upToFirstOccurrenceOf (" ", false, false).getIntValue()
                           : 12;

        const auto outPath = commandLine.contains ("--out")
                           ? commandLine.fromFirstOccurrenceOf ("--out", false, false).trim()
                                        .upToFirstOccurrenceOf (" ", false, false)
                           : juce::String();

        ListeningEngine engine;
        juce::StringArray log;

        for (const auto& name : ListeningEngine::availableOutputs())
            log.add ("salida disponible: " + name);

        // Permite apuntar a una salida concreta por indice, para barrer todas
        // desde un script sin pelearse con nombres que llevan espacios.
        juce::String target;

        if (commandLine.contains ("--device-index"))
        {
            const auto index = commandLine.fromFirstOccurrenceOf ("--device-index", false, false)
                                          .trim().upToFirstOccurrenceOf (" ", false, false).getIntValue();
            const auto all = ListeningEngine::availableOutputs();

            if (index > 0 && index < all.size())
                target = all[index];
        }

        log.add ("pedido: " + (target.isEmpty() ? juce::String ("(predeterminada)") : target));

        const auto error = engine.start (target);

        if (error.isNotEmpty())
        {
            log.add ("ERROR: " + error);
        }
        else
        {
            log.add ("escuchando: " + engine.reading().deviceName);

            juce::String lastChord;

            for (int tick = 0; tick < juce::jmax (1, seconds) * 4; ++tick)
            {
                juce::Thread::sleep (250);

                const auto reading = engine.reading();

                if (reading.chordSymbol.isNotEmpty() && reading.chordSymbol != lastChord)
                {
                    lastChord = reading.chordSymbol;
                    log.add ("acorde: " + lastChord
                             + "   confianza " + juce::String (reading.confidence, 3)
                             + "   margen " + juce::String (reading.margin, 3));
                }
            }

            const auto final = engine.reading();
            log.add ("llega audio: " + juce::String (final.receivingAudio ? "si" : "NO"));
            log.add ("hay musica: " + juce::String (final.hearingMusic ? "si" : "no"));
            log.add ("tonalidad: " + (final.keyName.isEmpty() ? juce::String ("(ninguna)") : final.keyName));
            log.add ("progresion: " + final.progression);
            log.add ("ventana de analisis: " + juce::String (final.windowSeconds, 3) + " s");
        }

        engine.stop();

        const auto text = log.joinIntoString ("\n") + "\n";

        if (outPath.isNotEmpty())
            juce::File (outPath).replaceWithText (text, false, false, "\n");
    }

    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (bool unattended)
            : juce::DocumentWindow ("Keyla",
                                    juce::Colour { 0xff1d1f24 },
                                    juce::DocumentWindow::allButtons)
        {
            auto* content = new MainComponent (unattended);
            content->onWakeRequested = [this] { wake(); };
            content->onSleepRequested = [this] { setMinimised (true); };

            setUsingNativeTitleBar (true);
            setContentOwned (content, true);
            setResizable (true, false);
            setResizeLimits (720, 340, 4000, 1400);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);

            // Arrancada por Windows: se aparta. No se ve y —lo que importa— no
            // ha abierto la tarjeta de sonido. Ver MainComponent.
            if (unattended)
                setMinimised (true);
        }

        void wake()
        {
            setMinimised (false);
            setVisible (true);
            toFront (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace keyla::app

START_JUCE_APPLICATION (keyla::app::KeylaApplication)
