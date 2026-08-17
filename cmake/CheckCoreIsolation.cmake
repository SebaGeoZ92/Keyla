# El aislamiento de core/ es una regla de compilación, no una convención
# (doc 03 §3). Si alguien incluye juce_gui_* o juce_audio_devices desde core/,
# el build falla aquí y no tres meses después, cuando desenredarlo sea caro.
#
# Se ejecuta como script:  cmake -DKEYLA_CORE_DIR=<dir> -P CheckCoreIsolation.cmake

if (NOT DEFINED KEYLA_CORE_DIR)
    message(FATAL_ERROR "CheckCoreIsolation: falta -DKEYLA_CORE_DIR")
endif()

set(forbidden
        "juce_gui_basics"
        "juce_gui_extra"
        "juce_graphics"
        "juce_audio_devices"
        "juce_audio_utils"
        "juce_audio_processors"
        "juce_opengl")

file(GLOB_RECURSE sources "${KEYLA_CORE_DIR}/*.h" "${KEYLA_CORE_DIR}/*.cpp")

set(violations "")

foreach (source IN LISTS sources)
    file(STRINGS "${source}" includeLines REGEX "^[ \t]*#[ \t]*include")

    foreach (line IN LISTS includeLines)
        foreach (module IN LISTS forbidden)
            if (line MATCHES "${module}")
                file(RELATIVE_PATH shortName "${KEYLA_CORE_DIR}" "${source}")
                list(APPEND violations "  ${shortName}:  ${line}")
            endif()
        endforeach()
    endforeach()
endforeach()

if (violations)
    string(REPLACE ";" "\n" report "${violations}")
    message(FATAL_ERROR
            "core/ no puede depender de JUCE gui ni de dispositivos (invariante 6):\n"
            "${report}\n"
            "  Los dispositivos y la UI viven en src/app/, detrás de interfaces que\n"
            "  define core/. Si necesitas algo de ahí, la interfaz va en core/ y la\n"
            "  implementación en app/.")
endif()
