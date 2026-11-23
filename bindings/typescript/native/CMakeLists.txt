cmake_minimum_required(VERSION 3.20)
project(NocturneWasmBinding LANGUAGES C)

find_package(Nocturne 1.0 CONFIG REQUIRED COMPONENTS nocturne OPTIONAL_COMPONENTS graph spatial simulation
             PATHS "${NOCTURNE_SDK_PREFIX}" NO_DEFAULT_PATH)

add_executable(nocturne_wasm bridge.c raw.c)
target_link_libraries(nocturne_wasm PRIVATE Nocturne::nocturne)
foreach(module IN ITEMS graph spatial simulation)
    if(TARGET Nocturne::${module})
        target_link_libraries(nocturne_wasm PRIVATE Nocturne::${module})
        target_compile_definitions(nocturne_wasm PRIVATE "NT_WASM_${module}" "NT_BINDING_${module}")
    endif()
endforeach()
target_link_options(nocturne_wasm PRIVATE -mexec-model=reactor -Wl,--export-memory)
set_target_properties(nocturne_wasm PROPERTIES OUTPUT_NAME "nocturne" SUFFIX ".wasm"
                       RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/product"
                       C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
