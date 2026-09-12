# Tests de core/. Sin hardware, sin audio y sin UI: ese es el punto.

add_executable(keyla_tests
        TransportTests.cpp
        LockFreeQueueTests.cpp
        KeyboardStateTests.cpp
        MusicTests.cpp
        InstrumentTests.cpp
        ExerciseTests.cpp
        EvaluationTests.cpp
        MidiImportTests.cpp
        ProgressionTests.cpp
        ListenTests.cpp)

target_link_libraries(keyla_tests PRIVATE
        keyla_core
        Catch2::Catch2WithMain)

keyla_apply_compiler_flags(keyla_tests)

# Una sola entrada en ctest que ejecuta la batería entera, en vez de
# catch_discover_tests().
#
# catch_discover_tests registra un test de ctest por nombre y luego se lo pasa
# al ejecutable como argumento de línea de comandos. Los nombres de aquí están
# en español, y en Windows los acentos no sobreviven ese viaje: Catch2 recibe
# "s+�lo" y responde "No test cases matched", con lo que el test se da por
# fallado sin haberse ejecutado. Peor todavía sería el caso simétrico — un test
# que no corre y se cuenta como pasado.
#
# Para ejecutar un subconjunto a mano:  keyla_tests.exe "[time]"
add_test(NAME keyla_core_tests COMMAND keyla_tests)
