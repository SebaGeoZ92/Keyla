# Catch2 para los tests de core/. Como JUCE: se descarga a libs/_deps la
# primera vez, o se usa una copia local con -DKEYLA_CATCH2_PATH=<ruta>.

set(KEYLA_CATCH2_TAG "v3.7.1" CACHE STRING "Tag de Catch2 a usar")
set(KEYLA_CATCH2_PATH "" CACHE PATH "Ruta a un checkout de Catch2 ya existente")

if (KEYLA_CATCH2_PATH)
    add_subdirectory("${KEYLA_CATCH2_PATH}" "${CMAKE_BINARY_DIR}/Catch2" EXCLUDE_FROM_ALL)
else()
    include(FetchContent)

    FetchContent_Declare(Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG ${KEYLA_CATCH2_TAG}
            GIT_SHALLOW TRUE
            GIT_PROGRESS TRUE)

    FetchContent_MakeAvailable(Catch2)
endif()

list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
