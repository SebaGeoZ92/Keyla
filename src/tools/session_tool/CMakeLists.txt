# keyla_session — analizar y ajustar sesiones de escucha grabadas.
#
# Existe aparte de la aplicación por una razón práctica que se repetía: para
# enlazar Keyla.exe hay que cerrarla, y el análisis de una sesión no tiene nada
# que ver con la ventana. Como herramienta de consola se puede compilar y
# ejecutar con Keyla abierta y el teclado sonando.

juce_add_console_app(keyla_session
        PRODUCT_NAME "keyla_session"
        COMPANY_NAME "Keyla")

target_sources(keyla_session PRIVATE
        main.cpp
        "${CMAKE_SOURCE_DIR}/src/app/ListeningSession.cpp")

target_include_directories(keyla_session PRIVATE "${CMAKE_SOURCE_DIR}/src/app")

target_compile_definitions(keyla_session PRIVATE
        JUCE_STANDALONE_APPLICATION=1
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_DISABLE_JUCE_VERSION_PRINTING=1)

target_link_libraries(keyla_session PRIVATE
        keyla_core
        juce::juce_core
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

keyla_apply_compiler_flags(keyla_session)
