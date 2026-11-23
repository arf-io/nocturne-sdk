cmake_minimum_required(VERSION 3.24)
project(NocturneBindingReplay LANGUAGES C)
find_package(Nocturne CONFIG REQUIRED)
add_executable(binding_replay replay.c)
set_target_properties(binding_replay PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_link_libraries(binding_replay PRIVATE Nocturne::nocturne)
