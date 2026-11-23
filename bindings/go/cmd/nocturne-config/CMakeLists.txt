cmake_minimum_required(VERSION 3.20)
project(NocturneGoSDK LANGUAGES C)

find_package(Nocturne 1.0 CONFIG REQUIRED COMPONENTS nocturne ${NOCTURNE_GO_COMPONENTS}
             PATHS "${NOCTURNE_SDK_PREFIX}" NO_DEFAULT_PATH)

get_target_property(includes Nocturne::nocturne INTERFACE_INCLUDE_DIRECTORIES)
string(REPLACE ";" "\n" includes "${includes}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/includes.txt" CONTENT "${includes}\n")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/target-system.txt"
     CONTENT "${CMAKE_SYSTEM_NAME}\n")

set(archives "")
foreach(component IN ITEMS posix_host windows_host graph simulation spatial nocturne)
    if(component STREQUAL "nocturne" OR component IN_LIST NOCTURNE_GO_COMPONENTS)
        string(APPEND archives "$<TARGET_FILE:Nocturne::${component}>\n")
    endif()
endforeach()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/archives.txt" CONTENT "${archives}")
