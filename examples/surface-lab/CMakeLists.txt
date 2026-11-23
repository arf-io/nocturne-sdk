cmake_minimum_required(VERSION 3.20)
project(NocturneSurfaceLab LANGUAGES C)

find_package(Nocturne 1.0 CONFIG REQUIRED COMPONENTS simulation)
add_executable(surface_lab main.c)
target_link_libraries(surface_lab PRIVATE Nocturne::simulation)
set_target_properties(surface_lab PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(surface_lab PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME surface_lab_frame COMMAND surface_lab --verify)
add_test(NAME surface_lab_plain COMMAND surface_lab 80)
