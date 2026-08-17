#pragma once

// El sufijo _u8 vive en core/text/Utf8.h, que es donde lo alcanza todo el
// proyecto. Aquí sólo se trae al namespace de la herramienta.

#include <core/text/Utf8.h>

namespace keyla::probe
{

using keyla::operator""_u8;

} // namespace keyla::probe
