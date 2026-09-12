// keyla_session — analizar y ajustar sesiones de escucha sin abrir la ventana.
//
//   keyla_session analyse [carpeta]              informe.txt
//   keyla_session tune    [carpeta] [--from s] [--to s]   ajuste.txt
//
// Sin carpeta, usa la sesión más reciente.

#include "ListeningSession.h"

#include <juce_core/juce_core.h>

#include <iostream>

namespace
{
    juce::File latestSession()
    {
        auto folders = keyla::app::listeningSessionsFolder()
                           .findChildFiles (juce::File::findDirectories, false, "escucha-*");

        // El nombre lleva la fecha en orden año-mes-día-hora: ordenar por nombre
        // es ordenar por tiempo.
        std::sort (folders.begin(), folders.end(),
                   [] (const juce::File& a, const juce::File& b) { return a.getFileName() < b.getFileName(); });

        return folders.isEmpty() ? juce::File() : folders.getLast();
    }

    double flag (const juce::StringArray& args, const char* name)
    {
        const auto index = args.indexOf (name);
        return index >= 0 && index + 1 < args.size() ? args[index + 1].getDoubleValue() : -1.0;
    }
}

int main (int argc, char* argv[])
{
    juce::StringArray args;

    for (int i = 1; i < argc; ++i)
        args.add (juce::String (juce::CharPointer_UTF8 (argv[i])));

    if (args.isEmpty() || (args[0] != "analyse" && args[0] != "tune"
                           && args[0] != "tune-all" && args[0] != "tune-bass"))
    {
        std::cout << "uso: keyla_session analyse|tune [carpeta] [--from s] [--to s]\n";
        return 2;
    }

    // "tune-all": todas las sesiones de más de dos minutos, a la vez. Las cortas
    // se dejan fuera porque suelen ser arranques en falso, y medio minuto
    // pesaría en la media lo mismo que una canción entera.
    const bool bass = args[0] == "tune-bass";

    if (args[0] == "tune-all" || bass || (args[0] == "tune" && args.size() > 2 && ! args[2].startsWith ("--")))
    {
        juce::Array<juce::File> folders;

        if (args[0] == "tune-all" || bass)
        {
            for (const auto& candidate : keyla::app::listeningSessionsFolder()
                                             .findChildFiles (juce::File::findDirectories, false, "escucha-*"))
            {
                keyla::app::ListeningSessionData data;
                juce::String ignored;

                if (keyla::app::readListeningSession (candidate, data, ignored) && data.durationSeconds() >= 120.0)
                    folders.add (candidate);
                else
                    std::cout << "(se deja fuera " << candidate.getFileName().toStdString() << ": menos de 2 min)\n";
            }
        }
        else
        {
            for (int i = 1; i < args.size(); ++i)
                folders.add (juce::File (args[i]));
        }

        std::sort (folders.begin(), folders.end(),
                   [] (const juce::File& a, const juce::File& b) { return a.getFileName() < b.getFileName(); });

        juce::String error;
        const auto output = keyla::app::listeningSessionsFolder()
                                .getChildFile (bass ? "ajuste-bajo.txt" : "ajuste-conjunto.txt");

        const auto text = keyla::app::tuneAcrossSessions (
            folders,
            bass ? keyla::app::bassTuningGrid() : keyla::app::generalTuningGrid(),
            bass ? "ajuste del bajo sobre varias canciones" : "ajuste sobre varias canciones",
            output, error);

        if (error.isNotEmpty())
        {
            std::cout << "ERROR: " << error.toStdString() << "\n";
            return 1;
        }

        std::cout << text.upToFirstOccurrenceOf ("TODOS", false, false).toStdString();
        return 0;
    }

    auto folder = args.size() > 1 && ! args[1].startsWith ("--") ? juce::File (args[1]) : latestSession();

    if (! folder.isDirectory())
    {
        std::cout << "No encuentro la sesion.\n";
        return 1;
    }

    juce::String error;
    const auto text = args[0] == "analyse"
                    ? keyla::app::analyseListeningSession (folder, error)
                    : keyla::app::tuneListeningSession (folder, flag (args, "--from"), flag (args, "--to"), error);

    if (error.isNotEmpty())
    {
        std::cout << "ERROR: " << error.toStdString() << "\n";
        return 1;
    }

    std::cout << text.toStdString();
    return 0;
}
