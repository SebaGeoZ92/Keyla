#pragma once

#include <core/listen/ListeningEvaluation.h>

#include <juce_core/juce_core.h>

#include <vector>

namespace keyla::app
{

/** Una sesión de escucha grabada: la canción tal como la oyó Keyla y lo que
    tocaste tú a la vez, en la misma escala de tiempo.

    Es el material con el que se ajusta el reconocimiento. Keyla no aprende
    sola —no hay nada ahí dentro que se modifique con el uso—, pero cada sesión
    en la que tocas siguiendo la canción es un examen corregido: tus teclas son
    la respuesta buena y lo que Keyla oyó es la respuesta del alumno.

    **Estas grabaciones no van a `tests/` ni a git.** Son canciones con
    derechos, grabadas para uso propio. Viven en `%APPDATA%\Keyla\sesiones` y de
    ahí no salen.
*/
struct ListeningSessionData
{
    double sampleRate { 0.0 };
    std::vector<std::int16_t> audio;          // mono
    std::vector<core::TimedNote> notes;       // segundos desde el inicio
    juce::String deviceName;

    double durationSeconds() const
    {
        return sampleRate > 0.0 ? static_cast<double> (audio.size()) / sampleRate : 0.0;
    }
};

/** Dónde se guardan. */
juce::File listeningSessionsFolder();

/** Escribe `audio.wav`, `notas.csv` y `sesion.txt` en una carpeta nueva con la
    fecha. Devuelve la carpeta, o un `File` vacío y el motivo en `error`. */
juce::File writeListeningSession (const ListeningSessionData& data, juce::String& error);

/** Lee una sesión escrita por `writeListeningSession`. */
bool readListeningSession (const juce::File& folder, ListeningSessionData& data, juce::String& error);

/** Vuelve a escuchar la grabación, la compara con lo que tocaste y escribe
    `informe.txt` en la misma carpeta. Devuelve el texto del informe.

    Se reanaliza el audio en vez de fiarse de lo que Keyla enseñó en directo,
    por dos motivos: es reproducible —la misma grabación da el mismo informe— y
    es lo que permite **probar otros ajustes sobre la misma canción** sin que
    tengas que volver a tocarla. */
juce::String analyseListeningSession (const juce::File& folder, juce::String& error);

} // namespace keyla::app
