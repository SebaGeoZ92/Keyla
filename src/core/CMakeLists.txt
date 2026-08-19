# keyla_core — el 70% del producto: modelo temporal, MIDI, evaluación,
# ejercicios. Portable y testeable sin hardware, sin audio y sin UI.
#
# Sólo módulos headless de JUCE (doc 03 §1). Nada de gui ni de dispositivos:
# lo vigila CheckCoreIsolation.cmake, que corre en cada build.

add_library(keyla_core STATIC
        audio/Limiter.h
        text/Utf8.h
        time/TimeSignature.h
        time/Transport.h
        time/Transport.cpp
        midi/MidiEvent.h
        midi/LockFreeQueue.h
        midi/KeyboardState.h
        midi/KeyboardState.cpp
        instrument/IInstrument.h
        instrument/StruckStringSynth.h
        instrument/StruckStringSynth.cpp
        instrument/PolyphonicInstrument.h
        instrument/Voices.h
        instrument/Instruments.h
        instrument/Instruments.cpp
        music/Pitch.h
        music/Pitch.cpp
        music/ChordRecognizer.h
        music/ChordRecognizer.cpp
        score/Exercise.h
        score/Exercise.cpp
        score/ExerciseGenerator.h
        score/ExerciseGenerator.cpp
        exercise/ExerciseRunner.h
        exercise/ExerciseRunner.cpp)

target_include_directories(keyla_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

target_link_libraries(keyla_core
        PUBLIC
            juce::juce_audio_basics
            juce::juce_core
        PRIVATE
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

target_compile_definitions(keyla_core PUBLIC
        JUCE_STANDALONE_APPLICATION=1
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_DISABLE_JUCE_VERSION_PRINTING=1)

keyla_apply_compiler_flags(keyla_core)

# ── El límite de arquitectura, como regla de build ──────────────────────────
add_custom_target(keyla_core_isolation ALL
        COMMAND "${CMAKE_COMMAND}"
                -DKEYLA_CORE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -P "${CMAKE_SOURCE_DIR}/cmake/CheckCoreIsolation.cmake"
        COMMENT "Comprobando que core/ no depende de JUCE gui ni de dispositivos"
        VERBATIM)

add_dependencies(keyla_core keyla_core_isolation)
