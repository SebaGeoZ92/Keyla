# Obtención de JUCE.
#
# Por defecto se clona en libs/_deps (ignorado por git). Si ya tienes una copia
# local, configura con  -DKEYLA_JUCE_PATH=C:/ruta/a/JUCE  y no se descarga nada.

set(KEYLA_JUCE_TAG "8.0.15" CACHE STRING "Tag de JUCE a usar")
set(KEYLA_JUCE_PATH "" CACHE PATH "Ruta a un checkout de JUCE ya existente")

if (KEYLA_JUCE_PATH)
    message(STATUS "Keyla: usando JUCE local en ${KEYLA_JUCE_PATH}")
    add_subdirectory("${KEYLA_JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
else()
    include(FetchContent)

    set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/libs/_deps" CACHE PATH
            "Dónde se descargan las dependencias" FORCE)

    message(STATUS "Keyla: descargando JUCE ${KEYLA_JUCE_TAG} (sólo la primera vez)")

    FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG ${KEYLA_JUCE_TAG}
            GIT_SHALLOW TRUE
            GIT_PROGRESS TRUE)

    FetchContent_MakeAvailable(JUCE)
endif()
