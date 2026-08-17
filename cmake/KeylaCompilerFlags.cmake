# Flags comunes. Se aplican con keyla_apply_compiler_flags(<target>) a nuestro
# código, nunca a JUCE (que compila con sus propios avisos).

function(keyla_apply_compiler_flags target)
    if (MSVC)
        target_compile_options(${target} PRIVATE
                /W4                 # avisos exigentes
                /permissive-        # conformidad estricta
                /Zc:__cplusplus     # __cplusplus con el valor real
                /utf-8              # los comentarios están en español
                /MP)                # compilación en paralelo
        # Sin excepciones ni RTTI no se puede: JUCE los usa. Se deja el default.
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()
