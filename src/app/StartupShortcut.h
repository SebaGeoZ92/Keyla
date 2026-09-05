#pragma once

#include <juce_core/juce_core.h>

namespace keyla::app::startup
{

/** Arranque con Windows.

    Se hace con un acceso directo en la carpeta de Inicio del usuario, no con
    la clave `Run` del registro. Cuesta más código pero es **visible**: se ve
    en el Explorador (`shell:startup`), se ve en el Administrador de tareas y
    se borra arrastrándolo a la papelera. Una entrada de registro que arranca
    programas sola y que sólo se puede quitar desde dentro del propio programa
    es exactamente el tipo de cosa que uno no quiere en su equipo.

    El acceso directo pasa `--startup`, y eso cambia cómo se comporta Keyla:
    ver `MainComponent`, sección de modo desatendido.
*/

/** El argumento que Windows le pasa a Keyla cuando la arranca él. */
inline constexpr const char* unattendedFlag = "--startup";

/** Dónde vive el acceso directo. Puede no existir. */
juce::File shortcutFile();

/** True si Keyla arrancará con Windows.

    La verdad está en el sistema de ficheros, no en `settings.json`: si se
    guardara también en los ajustes habría dos fuentes que pueden discrepar, y
    la casilla acabaría mintiendo el día que alguien borre el acceso directo a
    mano. */
bool isEnabled();

/** Crea o borra el acceso directo. Devuelve false y llena `error` si falla. */
bool setEnabled (bool shouldBeEnabled, juce::String& error);

/** Si el acceso directo existe pero apunta a otro sitio, lo reescribe.

    Sin esto, recompilar en otra carpeta rompe el arranque automático en
    silencio: la casilla seguiría marcada y Windows arrancaría un fichero que
    ya no está. */
void refreshTargetIfEnabled();

} // namespace keyla::app::startup
