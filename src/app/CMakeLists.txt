# keyla — la aplicación. La carcasa JUCE alrededor de core/.
#
# Aquí sí viven los dispositivos y la UI. Es la única parte del programa que
# puede hablar con juce_audio_devices y juce_gui_basics (doc 03 §3).

juce_add_gui_app(keyla
        PRODUCT_NAME "Keyla"
        COMPANY_NAME "Keyla"
        VERSION 0.1.0)

target_sources(keyla PRIVATE
        Main.cpp
        AudioDeviceHost.cpp
        SettingsStore.cpp
        MidiInputHost.cpp
        ui/MainComponent.cpp
        ui/ExerciseView.cpp
        ui/NowPlayingView.cpp
        ui/PianoKeyboardView.cpp)

target_compile_definitions(keyla PRIVATE
        JUCE_STANDALONE_APPLICATION=1
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_WINRT_MIDI=0            # MIDI clásico: menos capas, menos jitter
        JUCE_WASAPI=1
        JUCE_DIRECTSOUND=0               # 30-100 ms: no interesa ni como fallback
        JUCE_ASIO=$<BOOL:${KEYLA_ENABLE_ASIO}>
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_DISABLE_JUCE_VERSION_PRINTING=1)

if (KEYLA_ENABLE_ASIO)
    target_include_directories(keyla PRIVATE "${KEYLA_ASIO_SDK_PATH}")
endif()

target_link_libraries(keyla PRIVATE
        keyla_core
        juce::juce_audio_devices
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

keyla_apply_compiler_flags(keyla)
