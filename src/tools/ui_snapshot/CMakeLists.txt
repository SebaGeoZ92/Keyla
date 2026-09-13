# keyla_snapshot — dibuja la ventana de Keyla en un PNG sin abrirla.
#
# Para poder mirar la interfaz desde el código: toda la ventana se ha ido
# montando fila a fila sin ver el resultado, y opinar de ella sin verla sería
# inventárselo. Es un ejecutable aparte por lo mismo que keyla_session: con
# Keyla abierta no se puede enlazar Keyla.exe.

juce_add_console_app(keyla_snapshot
        PRODUCT_NAME "keyla_snapshot"
        COMPANY_NAME "Keyla")

target_sources(keyla_snapshot PRIVATE
        main.cpp
        "${CMAKE_SOURCE_DIR}/src/app/AudioDeviceHost.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/SettingsStore.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/StartupShortcut.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/LoopbackCapture.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/ListeningEngine.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/ListeningSession.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/MidiInputHost.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/ui/MainComponent.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/ui/ExerciseView.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/ui/NowPlayingView.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/ui/PianoKeyboardView.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/ui/ListeningView.cpp")

target_include_directories(keyla_snapshot PRIVATE "${CMAKE_SOURCE_DIR}/src/app")

target_compile_definitions(keyla_snapshot PRIVATE
        JUCE_STANDALONE_APPLICATION=1
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_WINRT_MIDI=0
        JUCE_WASAPI=1
        JUCE_DIRECTSOUND=0
        JUCE_ASIO=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_DISABLE_JUCE_VERSION_PRINTING=1)

target_link_libraries(keyla_snapshot PRIVATE
        keyla_core
        juce::juce_audio_devices
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if (WIN32)
    target_link_libraries(keyla_snapshot PRIVATE ole32 shell32 uuid)
endif()

keyla_apply_compiler_flags(keyla_snapshot)
