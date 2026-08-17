#pragma once

// `juce::String (const char*)` construye con CharPointer_ASCII: interpreta cada
// byte suelto como Latin-1, no como UTF-8. Con los ficheros en UTF-8 y /utf-8 en
// el compilador, un literal con acento son dos bytes que acaban convertidos en
// dos caracteres y salen doblemente codificados por consola ("sÃ³lo").
//
// Como la documentación y los mensajes de este proyecto van en español, todo
// literal con acentos, eñes o signos tipográficos tiene que llevar el sufijo:
//
//     out ("Latencia de salida estimada"_u8 + juce::String (x));

#include <juce_core/juce_core.h>

#include <cstddef>

namespace keyla::probe
{

inline juce::String operator""_u8 (const char* utf8Text, std::size_t)
{
    return juce::String (juce::CharPointer_UTF8 (utf8Text));
}

} // namespace keyla::probe
