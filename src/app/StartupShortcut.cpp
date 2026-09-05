#include "StartupShortcut.h"

#if JUCE_WINDOWS
 #include <windows.h>
 #include <shlobj.h>
 #include <objbase.h>
#endif

namespace keyla::app::startup
{

namespace
{
    const char* shortcutName = "Keyla.lnk";
}

#if JUCE_WINDOWS

namespace
{
    /** COM inicializado mientras dure el ámbito, y sólo si hacía falta.

        `RPC_E_CHANGED_MODE` significa que otro hilo ya lo inicializó en otro
        modelo: se puede usar igualmente y **no** hay que llamar a
        `CoUninitialize`, porque no fuimos nosotros quienes lo abrimos. JUCE
        inicializa COM por su cuenta en el message thread, así que este caso es
        el normal, no el raro. */
    struct ComScope
    {
        ComScope() : result (CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED)) {}
        ~ComScope() { if (SUCCEEDED (result)) CoUninitialize(); }

        bool usable() const { return SUCCEEDED (result) || result == RPC_E_CHANGED_MODE; }

        HRESULT result;
    };

    juce::File startupFolder()
    {
        PWSTR path = nullptr;

        if (SUCCEEDED (SHGetKnownFolderPath (FOLDERID_Startup, 0, nullptr, &path)) && path != nullptr)
        {
            const juce::File folder { juce::String (path) };
            CoTaskMemFree (path);

            if (folder.isDirectory())
                return folder;
        }

        // Reserva por si SHGetKnownFolderPath falla. En Windows moderno estos
        // nombres de carpeta están en inglés en disco aunque el Explorador los
        // muestre traducidos, así que la ruta construida a mano vale.
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Microsoft")
                   .getChildFile ("Windows")
                   .getChildFile ("Start Menu")
                   .getChildFile ("Programs")
                   .getChildFile ("Startup");
    }

    juce::File readShortcutTarget (const juce::File& link)
    {
        ComScope com;

        if (! com.usable())
            return {};

        IShellLinkW* shellLink = nullptr;

        if (FAILED (CoCreateInstance (CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_IShellLinkW, reinterpret_cast<void**> (&shellLink))))
            return {};

        juce::File target;
        IPersistFile* persist = nullptr;

        if (SUCCEEDED (shellLink->QueryInterface (IID_IPersistFile,
                                                  reinterpret_cast<void**> (&persist))))
        {
            if (SUCCEEDED (persist->Load (link.getFullPathName().toWideCharPointer(), STGM_READ)))
            {
                wchar_t buffer[MAX_PATH] = {};

                if (SUCCEEDED (shellLink->GetPath (buffer, MAX_PATH, nullptr, 0)) && buffer[0] != 0)
                    target = juce::File { juce::String (buffer) };
            }

            persist->Release();
        }

        shellLink->Release();
        return target;
    }

    bool writeShortcut (const juce::File& link, const juce::File& target, juce::String& error)
    {
        ComScope com;

        if (! com.usable())
        {
            error = "Windows no dejo inicializar COM.";
            return false;
        }

        IShellLinkW* shellLink = nullptr;

        if (FAILED (CoCreateInstance (CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_IShellLinkW, reinterpret_cast<void**> (&shellLink))))
        {
            error = "Windows no dejo crear el acceso directo.";
            return false;
        }

        shellLink->SetPath (target.getFullPathName().toWideCharPointer());
        shellLink->SetArguments (juce::String (unattendedFlag).toWideCharPointer());
        shellLink->SetWorkingDirectory (target.getParentDirectory().getFullPathName().toWideCharPointer());
        shellLink->SetDescription (L"Keyla");

        bool ok = false;
        IPersistFile* persist = nullptr;

        if (SUCCEEDED (shellLink->QueryInterface (IID_IPersistFile,
                                                  reinterpret_cast<void**> (&persist))))
        {
            ok = SUCCEEDED (persist->Save (link.getFullPathName().toWideCharPointer(), TRUE));
            persist->Release();
        }

        shellLink->Release();

        if (! ok)
            error = "No se pudo escribir en la carpeta de Inicio.";

        return ok;
    }
}

juce::File shortcutFile()
{
    return startupFolder().getChildFile (shortcutName);
}

bool isEnabled()
{
    return shortcutFile().existsAsFile();
}

bool setEnabled (bool shouldBeEnabled, juce::String& error)
{
    const auto link = shortcutFile();

    if (! shouldBeEnabled)
    {
        if (link.existsAsFile() && ! link.deleteFile())
        {
            error = "No se pudo borrar el acceso directo de la carpeta de Inicio.";
            return false;
        }

        return true;
    }

    const auto target = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

    if (! link.getParentDirectory().isDirectory())
    {
        error = "No encuentro la carpeta de Inicio de Windows.";
        return false;
    }

    return writeShortcut (link, target, error);
}

void refreshTargetIfEnabled()
{
    const auto link = shortcutFile();

    if (! link.existsAsFile())
        return;

    const auto target = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

    if (readShortcutTarget (link) == target)
        return;

    juce::String ignored;
    writeShortcut (link, target, ignored);
}

#else

// Keyla es de Windows. El resto existe sólo para que core/ y la app compilen
// en otro sitio si alguna vez hace falta, y no promete nada.
juce::File shortcutFile()                        { return {}; }
bool isEnabled()                                 { return false; }
bool setEnabled (bool, juce::String& error)      { error = "Solo en Windows."; return false; }
void refreshTargetIfEnabled()                    {}

#endif

} // namespace keyla::app::startup
