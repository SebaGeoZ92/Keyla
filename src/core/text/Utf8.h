#pragma once

// `juce::String (const char*)` construye con CharPointer_ASCII: interpreta cada
// byte suelto como Latin-1, no como UTF-8. Con los ficheros en UTF-8 y /utf-8 en
// el compilador, un literal con acento son dos bytes que acaban convertidos en
// dos caracteres y salen doblemente codificados ("sÃ³lo", "Â·").
//
// No hay aviso del compilador. Sólo se ve cuando el texto llega a la pantalla,
// que es tarde.
//
// Como la documentación y los mensajes de este proyecto van en español, todo
// literal con acentos, eñes o signos tipográficos que acabe en un juce::String
// lleva el sufijo:
//
//     label.setText ("Salida inalámbrica"_u8, juce::dontSendNotification);
//     out ("Latencia: "_u8 + juce::String (x));
//
// En una concatenación de literales adyacentes basta con ponérselo al último:
// el sufijo se aplica al conjunto.

#include <juce_core/juce_core.h>

#include <cstddef>

namespace keyla
{

inline juce::String operator""_u8 (const char* utf8Text, std::size_t)
{
    return juce::String (juce::CharPointer_UTF8 (utf8Text));
}

} // namespace keyla
