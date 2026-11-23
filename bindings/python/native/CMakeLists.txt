cmake_minimum_required(VERSION 3.20)
project(NocturnePythonSDK LANGUAGES C)

find_package(Nocturne 1.0 CONFIG REQUIRED COMPONENTS nocturne ${NOCTURNE_PYTHON_COMPONENTS}
             PATHS "${NOCTURNE_SDK_PREFIX}" NO_DEFAULT_PATH)
get_target_property(includes Nocturne::nocturne INTERFACE_INCLUDE_DIRECTORIES)
string(REPLACE ";" "\n" includes "${includes}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/include-dirs" CONTENT "${includes}\n")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/compiler" CONTENT "${CMAKE_C_COMPILER}\n")
set(compiler_frontend "${CMAKE_C_COMPILER_FRONTEND_VARIANT}")
if(MSVC AND NOT compiler_frontend)
    set(compiler_frontend MSVC)
endif()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/compiler-frontend"
     CONTENT "${compiler_frontend}\n")

set(archives "")
foreach(component IN ITEMS posix_host graph simulation spatial nocturne)
    if(component STREQUAL "nocturne" OR component IN_LIST NOCTURNE_PYTHON_COMPONENTS)
        string(APPEND archives "$<TARGET_FILE:Nocturne::${component}>\n")
    endif()
endforeach()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/archives" CONTENT "${archives}")
