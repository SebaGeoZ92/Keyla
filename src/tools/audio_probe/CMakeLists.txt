# audio_probe — spike de latencia de la fase 0 (doc 05).
#
# Ejecutable de consola: sin UI, sin samples. Su único propósito es responder
# si este PC cumple los criterios del doc 04 §8.

juce_add_console_app(audio_probe
        PRODUCT_NAME "audio_probe"
        COMPANY_NAME "Keyla")

target_sources(audio_probe PRIVATE
        main.cpp
        ProbeOptions.cpp
        ProbeEngine.cpp
        MidiPulseGenerator.cpp
        LoopbackCalibrator.cpp
        Report.cpp)

target_compile_definitions(audio_probe PRIVATE
        JUCE_STANDALONE_APPLICATION=1
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_MODAL_LOOPS_PERMITTED=1     # el bucle de consola bombea mensajes
        JUCE_USE_WINRT_MIDI=0            # MIDI clásico: menos capas, menos jitter
        JUCE_WASAPI=1
        JUCE_DIRECTSOUND=0               # 30–100 ms: no interesa ni como fallback
        JUCE_ASIO=$<BOOL:${KEYLA_ENABLE_ASIO}>
        JUCE_DISABLE_JUCE_VERSION_PRINTING=1)

if (KEYLA_ENABLE_ASIO)
    if (NOT KEYLA_ASIO_SDK_PATH)
        message(FATAL_ERROR
                "KEYLA_ENABLE_ASIO=ON requiere -DKEYLA_ASIO_SDK_PATH=<ruta>/common. "
                "El SDK de Steinberg no se versiona en este repo (doc 03 §2).")
    endif()
    target_include_directories(audio_probe PRIVATE "${KEYLA_ASIO_SDK_PATH}")
endif()

target_link_libraries(audio_probe PRIVATE
        keyla_core
        juce::juce_audio_devices
        juce::juce_audio_basics
        juce::juce_events
        juce::juce_core
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

keyla_apply_compiler_flags(audio_probe)
