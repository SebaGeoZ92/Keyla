// keyla_snapshot [salida.png]
//
// Monta la ventana de Keyla en modo desatendido —sin abrir la tarjeta de
// sonido, para no pelearse con una Keyla que esté sonando— y la dibuja en un
// PNG tal como se vería.

#include "ui/MainComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <iostream>

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;

    juce::StringArray args;

    for (int i = 1; i < argc; ++i)
        args.add (juce::String (juce::CharPointer_UTF8 (argv[i])));

    const auto outputName = [&args]
    {
        for (const auto& arg : args)
            if (! arg.startsWith ("--"))
                return arg;

        return juce::String ("keyla.png");
    }();

    const juce::File output = juce::File::getCurrentWorkingDirectory().getChildFile (outputName);

    auto component = std::make_unique<keyla::app::MainComponent> (true);

    // --demo: con contenido de muestra. --settings: con el panel de ajustes abierto.
    if (args.contains ("--demo"))
        component->applyPreviewState();

    if (args.contains ("--settings"))
        component->showSettings (true);

    // **No se deja correr el bucle de mensajes.** La primera versión lo hacía
    // 400 ms para que se rellenaran los textos del temporizador, y en ese rato
    // la ventana detectaba el teclado MIDI, salía del modo desatendido y abría
    // la salida de audio en exclusivo — con Keyla sonando al lado. Una
    // herramienta para mirar no puede tocar dispositivos, así que la foto sale
    // con la barra de estado y el mensaje vacíos y la disposición intacta.

    const auto image = component->createComponentSnapshot (component->getLocalBounds(), true, 1.0f);

    juce::FileOutputStream stream (output);

    if (! stream.openedOk())
    {
        std::cout << "No se pudo escribir " << output.getFullPathName().toStdString() << "\n";
        return 1;
    }

    stream.setPosition (0);
    stream.truncate();
    juce::PNGImageFormat().writeImageToStream (image, stream);

    std::cout << output.getFullPathName().toStdString() << "  "
              << image.getWidth() << "x" << image.getHeight() << "\n";

    component.reset();
    return 0;
}
