cmake_minimum_required(VERSION 3.22)

project(Keyla
        VERSION 0.0.1
        DESCRIPTION "Keyla — aprender piano con teclado MIDI"
        LANGUAGES C CXX)   # JUCE necesita C para algunas de sus dependencias

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

# ── Estándar y tipo de build ────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Un build Debug invalida cualquier medida de carga de CPU. Release por defecto.
get_property(_keyla_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (NOT _keyla_multi_config AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Tipo de build" FORCE)
endif()

# CRT estático: los ejecutables de tools/ se copian y se ejecutan sueltos.
if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ── Opciones del proyecto ───────────────────────────────────────────────────
# El SDK de ASIO no es redistribuible y no se versiona en este repo (doc 03 §2).
option(KEYLA_ENABLE_ASIO "Compilar con soporte ASIO (requiere el SDK de Steinberg)" OFF)
set(KEYLA_ASIO_SDK_PATH "" CACHE PATH "Ruta al SDK de ASIO de Steinberg (common/)")

option(KEYLA_BUILD_TESTS "Compilar los tests de core/" ON)

include(KeylaCompilerFlags)
include(FetchJUCE)

add_subdirectory(src/core)
add_subdirectory(src/app)
add_subdirectory(src/tools/audio_probe)
add_subdirectory(src/tools/session_tool)
add_subdirectory(src/tools/ui_snapshot)

if (KEYLA_BUILD_TESTS)
    enable_testing()
    include(FetchCatch2)
    add_subdirectory(tests)
endif()
