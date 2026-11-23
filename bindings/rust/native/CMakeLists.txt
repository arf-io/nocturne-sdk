cmake_minimum_required(VERSION 3.20)
project(NocturneRustSDK LANGUAGES C)

find_package(Nocturne 1.0 CONFIG REQUIRED COMPONENTS nocturne ${NOCTURNE_RUST_COMPONENTS}
             PATHS "${NOCTURNE_SDK_PREFIX}" NO_DEFAULT_PATH)

get_target_property(include_dirs Nocturne::nocturne INTERFACE_INCLUDE_DIRECTORIES)
string(REPLACE ";" "\n" include_dirs "${include_dirs}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/include-dirs"
     CONTENT "${include_dirs}\n")

# Dependents precede their dependencies in the static link order.
set(archives "")
foreach(component IN ITEMS posix_host graph simulation spatial nocturne)
    if(component STREQUAL "nocturne" OR component IN_LIST NOCTURNE_RUST_COMPONENTS)
        string(APPEND archives "${component}\t$<TARGET_FILE:Nocturne::${component}>\n")
    endif()
endforeach()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/archives" CONTENT "${archives}")
