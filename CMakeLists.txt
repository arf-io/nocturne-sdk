cmake_minimum_required(VERSION 3.20)
project(Nocturne VERSION 1.0.0 LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)
option(NOCTURNE_BUILD_EXAMPLES "Build the SDK consumer example" ON)
option(NOCTURNE_BUILD_GRAPH "Build the graph layout and view extension" ON)
option(NOCTURNE_BUILD_SPATIAL "Build the spatial geometry extension" ON)
include(CMakeDependentOption)
cmake_dependent_option(NOCTURNE_BUILD_SIMULATION "Build the surface simulation extension"
                       ON "NOCTURNE_BUILD_SPATIAL" OFF)
option(NOCTURNE_SANITIZE "Enable address and undefined behavior sanitizers" OFF)
set(NOCTURNE_POSIX_AVAILABLE OFF)
if(UNIX AND NOT CMAKE_SYSTEM_NAME MATCHES "^(Emscripten|WASI)$")
    set(NOCTURNE_POSIX_AVAILABLE ON)
endif()
option(NOCTURNE_BUILD_POSIX_HOST "Build the POSIX terminal adapter" ${NOCTURNE_POSIX_AVAILABLE})
option(NOCTURNE_BUILD_WINDOWS_HOST "Build the Windows console adapter" ${WIN32})

add_library(nocturne STATIC
    src/abi.c
    src/animation.c
    src/spring.c
    src/spring_range.c
    src/track.c
    src/timeline.c
    src/motion.c
    src/utf8.c
    src/grapheme.c
    src/text.c
    src/glyph_width.c
    src/linebreak.c
    src/bidi.c
    src/script_runs.c
    src/text_layout.c
    src/editor.c
    src/editor_input.c
    src/frame.c
    src/color.c
    src/paint_sample.c
    src/panel_transform.c
    src/surface.c
    src/blend.c
    src/sampling.c
    src/canvas.c
    src/content.c
    src/text_paint.c
    src/text_effect.c
    src/present.c
    src/component.c
    src/property.c
    src/schedule.c
    src/state.c
    src/plot_geometry.c
    src/plot.c
    src/digits.c
    src/binding.c
    src/style.c
    src/component_layout.c
    src/component_input.c
    src/component_intent.c
    src/event.c
    src/event_rules.c
    src/input.c
    src/input_sequences.c
    src/box_layout.c
    src/viewport.c
    src/window.c
    src/notification.c
    src/panel_content.c
    src/particle.c
    src/particle_deposits.c
    src/particle_effect.c
    src/particle_view.c
    src/visual_branch.c
    src/field.c
    src/widgets.c
    src/controls.c
    src/choices.c
    src/indicators.c
    src/editor_view.c)
add_library(Nocturne::nocturne ALIAS nocturne)
target_compile_features(nocturne PUBLIC c_std_99)
set_target_properties(nocturne PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
                      POSITION_INDEPENDENT_CODE ON)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set_property(SOURCE src/field.c src/panel_transform.c src/particle.c src/particle_effect.c src/particle_view.c src/plot_geometry.c src/spring.c src/spring_range.c src/surface.c
                 APPEND PROPERTY COMPILE_OPTIONS -frounding-math)
endif()
target_include_directories(nocturne PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
if(NOT WIN32)
    target_link_libraries(nocturne PUBLIC m)
endif()
if(NOT MSVC)
    target_compile_options(nocturne PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
if(NOCTURNE_SANITIZE AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(nocturne PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(nocturne PUBLIC -fsanitize=address,undefined)
endif()

if(NOCTURNE_BUILD_POSIX_HOST)
    if(NOT NOCTURNE_POSIX_AVAILABLE)
        message(FATAL_ERROR "NOCTURNE_BUILD_POSIX_HOST requires a POSIX terminal platform")
    endif()
    add_library(nocturne_posix_host STATIC src/posix_host.c)
    add_library(Nocturne::posix_host ALIAS nocturne_posix_host)
    target_link_libraries(nocturne_posix_host PUBLIC Nocturne::nocturne)
    target_compile_options(nocturne_posix_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
    set_target_properties(nocturne_posix_host PROPERTIES EXPORT_NAME posix_host
                          C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
                          POSITION_INDEPENDENT_CODE ON)
    install(TARGETS nocturne_posix_host EXPORT NocturneTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(FILES include/nocturne/posix_host.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nocturne)
endif()

if(NOCTURNE_BUILD_WINDOWS_HOST)
    if(NOT WIN32)
        message(FATAL_ERROR "NOCTURNE_BUILD_WINDOWS_HOST requires Windows")
    endif()
    add_library(nocturne_windows_host STATIC src/windows_host.c)
    add_library(Nocturne::windows_host ALIAS nocturne_windows_host)
    target_link_libraries(nocturne_windows_host PUBLIC Nocturne::nocturne)
    if(NOT MSVC)
        target_compile_options(nocturne_windows_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    set_target_properties(nocturne_windows_host PROPERTIES EXPORT_NAME windows_host
                          C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    install(TARGETS nocturne_windows_host EXPORT NocturneTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(FILES include/nocturne/windows_host.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nocturne)
endif()

if(NOCTURNE_BUILD_GRAPH)
    add_library(nocturne_graph STATIC src/graph_layout.c src/graph_view.c)
    add_library(Nocturne::graph ALIAS nocturne_graph)
    target_link_libraries(nocturne_graph PUBLIC Nocturne::nocturne)
    set_target_properties(nocturne_graph PROPERTIES EXPORT_NAME graph
                          C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
                          POSITION_INDEPENDENT_CODE ON)
    if(NOT MSVC)
        target_compile_options(nocturne_graph PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    install(TARGETS nocturne_graph EXPORT NocturneTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(FILES include/nocturne/graph.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nocturne)
endif()

if(NOCTURNE_BUILD_SPATIAL)
    add_library(nocturne_spatial STATIC src/spatial.c src/spatial_path.c src/spatial_exact.c
                src/spatial_projection.c src/spatial_clip.c src/spatial_pick.c
                src/spatial_mesh.c src/spatial_obj.c src/spatial_polygon.c src/spatial_raster.c
                src/spatial_surface_pose.c src/spatial_contact.c)
    add_library(Nocturne::spatial ALIAS nocturne_spatial)
    target_link_libraries(nocturne_spatial PUBLIC Nocturne::nocturne)
    set_target_properties(nocturne_spatial PROPERTIES EXPORT_NAME spatial
                          C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
                          POSITION_INDEPENDENT_CODE ON)
    if(NOT MSVC)
        target_compile_options(nocturne_spatial PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_compile_options(nocturne_spatial PRIVATE -frounding-math)
    endif()
    install(TARGETS nocturne_spatial EXPORT NocturneTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(FILES include/nocturne/spatial.h include/nocturne/spatial_mesh.h include/nocturne/spatial_surface.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nocturne)
endif()

if(NOCTURNE_BUILD_SIMULATION)
    add_library(nocturne_simulation STATIC src/simulation_wave.c src/simulation_flow.c src/simulation_spray.c
                src/simulation_transport.c src/simulation_hull.c src/simulation_immersion.c
                src/simulation_hull_measure.c src/simulation_response.c src/simulation_coupling.c
                src/simulation_body.c src/simulation_wide.c)
    add_library(Nocturne::simulation ALIAS nocturne_simulation)
    target_link_libraries(nocturne_simulation PUBLIC Nocturne::spatial)
    set_target_properties(nocturne_simulation PROPERTIES EXPORT_NAME simulation
                          C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
                          POSITION_INDEPENDENT_CODE ON)
    if(NOT MSVC)
        target_compile_options(nocturne_simulation PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_compile_options(nocturne_simulation PRIVATE -frounding-math)
    endif()
    install(TARGETS nocturne_simulation EXPORT NocturneTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(FILES include/nocturne/simulation.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nocturne)
endif()

if(BUILD_TESTING)
    find_package(Threads QUIET)
    add_executable(nocturne_abi_conformance tests/consumer/abi_conformance.c)
    target_compile_definitions(nocturne_abi_conformance PRIVATE NOCTURNE_ABI_TEST_MAIN)
    set_target_properties(nocturne_abi_conformance PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_link_libraries(nocturne_abi_conformance PRIVATE Nocturne::nocturne)
    add_test(NAME abi_conformance COMMAND nocturne_abi_conformance)
    if(TARGET Nocturne::simulation)
        add_executable(nocturne_surface_flow_contract tests/consumer/surface_flow.c)
        target_link_libraries(nocturne_surface_flow_contract PRIVATE Nocturne::simulation)
        add_test(NAME surface_flow COMMAND nocturne_surface_flow_contract)
        add_executable(nocturne_surface_transport_contract tests/consumer/surface_transport.c)
        target_link_libraries(nocturne_surface_transport_contract PRIVATE Nocturne::simulation)
        add_test(NAME surface_transport COMMAND nocturne_surface_transport_contract)
        add_executable(nocturne_hull_immersion_contract tests/consumer/hull_immersion.c)
        target_link_libraries(nocturne_hull_immersion_contract PRIVATE Nocturne::simulation)
        add_test(NAME hull_immersion COMMAND nocturne_hull_immersion_contract)
        add_executable(nocturne_fluid_coupling_contract tests/consumer/fluid_coupling.c)
        target_link_libraries(nocturne_fluid_coupling_contract PRIVATE Nocturne::simulation)
        add_test(NAME fluid_coupling COMMAND nocturne_fluid_coupling_contract)
        if(Threads_FOUND AND CMAKE_USE_PTHREADS_INIT)
            foreach(contract IN ITEMS surface_transport hull_immersion fluid_coupling)
                target_compile_definitions(nocturne_${contract}_contract PRIVATE NOCTURNE_TEST_PTHREAD)
                target_link_libraries(nocturne_${contract}_contract PRIVATE Threads::Threads)
            endforeach()
        endif()
    endif()
    if(TARGET Nocturne::spatial)
        add_executable(nocturne_spatial_contract tests/consumer/spatial.c)
        target_link_libraries(nocturne_spatial_contract PRIVATE Nocturne::spatial)
        add_test(NAME spatial COMMAND nocturne_spatial_contract)
        add_executable(nocturne_spatial_projection_contract tests/consumer/spatial_projection.c)
        target_link_libraries(nocturne_spatial_projection_contract PRIVATE Nocturne::spatial)
        add_test(NAME spatial_projection COMMAND nocturne_spatial_projection_contract)
        add_executable(nocturne_spatial_mesh_contract tests/consumer/spatial_mesh.c)
        target_link_libraries(nocturne_spatial_mesh_contract PRIVATE Nocturne::spatial)
        add_test(NAME spatial_mesh COMMAND nocturne_spatial_mesh_contract)
        add_executable(nocturne_spatial_surface_contract tests/consumer/spatial_surface.c)
        target_link_libraries(nocturne_spatial_surface_contract PRIVATE Nocturne::spatial)
        add_test(NAME spatial_surface COMMAND nocturne_spatial_surface_contract)
    endif()
    add_executable(nocturne_particles tests/consumer/particles.c)
    target_link_libraries(nocturne_particles PRIVATE Nocturne::nocturne)
    add_test(NAME particles COMMAND nocturne_particles)
    add_executable(nocturne_particle_deposits tests/consumer/particle_deposits.c)
    target_link_libraries(nocturne_particle_deposits PRIVATE Nocturne::nocturne)
    add_test(NAME particle_deposits COMMAND nocturne_particle_deposits)
    add_executable(nocturne_particle_effects tests/consumer/particle_effects.c)
    target_link_libraries(nocturne_particle_effects PRIVATE Nocturne::nocturne)
    add_test(NAME particle_effects COMMAND nocturne_particle_effects)
    add_executable(nocturne_fields tests/consumer/fields.c)
    target_link_libraries(nocturne_fields PRIVATE Nocturne::nocturne)
    add_test(NAME fields COMMAND nocturne_fields)
    add_executable(nocturne_window tests/consumer/window.c)
    target_link_libraries(nocturne_window PRIVATE Nocturne::nocturne)
    add_test(NAME window COMMAND nocturne_window)
    add_executable(nocturne_notification tests/consumer/notification.c)
    target_link_libraries(nocturne_notification PRIVATE Nocturne::nocturne)
    add_test(NAME notification COMMAND nocturne_notification)
    if(TARGET Nocturne::graph)
        add_executable(nocturne_graph_layout tests/consumer/graph_layout.c)
        target_link_libraries(nocturne_graph_layout PRIVATE Nocturne::graph)
        add_test(NAME graph_layout COMMAND nocturne_graph_layout)
        add_executable(nocturne_graph_view tests/consumer/graph_view.c)
        target_link_libraries(nocturne_graph_view PRIVATE Nocturne::graph)
        add_test(NAME graph_view COMMAND nocturne_graph_view)
        add_executable(nocturne_graph_workflow tests/consumer/graph_workflow.c)
        target_link_libraries(nocturne_graph_workflow PRIVATE Nocturne::graph)
        add_test(NAME graph_workflow COMMAND nocturne_graph_workflow)
    endif()
    add_executable(nocturne_plot tests/consumer/plot.c)
    target_link_libraries(nocturne_plot PRIVATE Nocturne::nocturne)
    add_test(NAME plot COMMAND nocturne_plot)
    add_executable(nocturne_state_update tests/consumer/state_update.c)
    target_link_libraries(nocturne_state_update PRIVATE Nocturne::nocturne)
    add_test(NAME state_update COMMAND nocturne_state_update)
    add_executable(nocturne_event_rules tests/consumer/event_rules.c)
    target_link_libraries(nocturne_event_rules PRIVATE Nocturne::nocturne)
    add_test(NAME event_rules COMMAND nocturne_event_rules)
    add_executable(nocturne_style tests/consumer/style.c)
    target_link_libraries(nocturne_style PRIVATE Nocturne::nocturne)
    add_test(NAME style COMMAND nocturne_style)
    add_executable(nocturne_binding tests/consumer/binding.c)
    target_link_libraries(nocturne_binding PRIVATE Nocturne::nocturne)
    add_test(NAME binding COMMAND nocturne_binding)
    add_executable(nocturne_property_provider tests/consumer/property_provider.c)
    target_link_libraries(nocturne_property_provider PRIVATE Nocturne::nocturne)
    add_test(NAME property_provider COMMAND nocturne_property_provider)
    add_executable(nocturne_panel_transform tests/consumer/panel_transform.c)
    target_link_libraries(nocturne_panel_transform PRIVATE Nocturne::nocturne)
    add_test(NAME panel_transform COMMAND nocturne_panel_transform)
    add_executable(nocturne_text_effect tests/consumer/text_effect.c)
    target_link_libraries(nocturne_text_effect PRIVATE Nocturne::nocturne)
    add_test(NAME text_effect COMMAND nocturne_text_effect)
    add_executable(nocturne_timeline tests/consumer/timeline.c)
    target_link_libraries(nocturne_timeline PRIVATE Nocturne::nocturne)
    add_test(NAME timeline COMMAND nocturne_timeline)
    add_executable(nocturne_spring tests/consumer/spring.c)
    target_link_libraries(nocturne_spring PRIVATE Nocturne::nocturne)
    add_test(NAME spring COMMAND nocturne_spring)
    add_executable(nocturne_contracts tests/contracts.c)
    target_link_libraries(nocturne_contracts PRIVATE Nocturne::nocturne)
    add_test(NAME component_contracts COMMAND nocturne_contracts)
    add_executable(nocturne_unicode_grapheme tests/unicode_grapheme.c)
    target_link_libraries(nocturne_unicode_grapheme PRIVATE Nocturne::nocturne)
    add_test(NAME unicode_grapheme COMMAND nocturne_unicode_grapheme
             "${CMAKE_CURRENT_SOURCE_DIR}/unicode/17.0.0/GraphemeBreakTest.txt")
    add_executable(nocturne_unicode_analysis tests/unicode_analysis.c)
    target_link_libraries(nocturne_unicode_analysis PRIVATE Nocturne::nocturne)
    add_test(NAME unicode_analysis COMMAND nocturne_unicode_analysis
             "${CMAKE_CURRENT_SOURCE_DIR}/unicode/17.0.0/LineBreakTest.txt"
             "${CMAKE_CURRENT_SOURCE_DIR}/unicode/17.0.0/BidiTest.txt"
             "${CMAKE_CURRENT_SOURCE_DIR}/unicode/17.0.0/BidiCharacterTest.txt")
    add_executable(nocturne_text_analysis_contracts tests/text_analysis_contracts.c)
    target_link_libraries(nocturne_text_analysis_contracts PRIVATE Nocturne::nocturne)
    add_test(NAME text_analysis_contracts COMMAND nocturne_text_analysis_contracts)
    add_executable(nocturne_text_layout tests/text_layout.c)
    target_link_libraries(nocturne_text_layout PRIVATE Nocturne::nocturne)
    add_test(NAME text_layout COMMAND nocturne_text_layout)
    add_executable(nocturne_text_storage tests/text_storage.c)
    target_link_libraries(nocturne_text_storage PRIVATE Nocturne::nocturne)
    add_test(NAME text_storage COMMAND nocturne_text_storage)
    add_executable(nocturne_paint_sampling tests/paint_sampling.c)
    target_link_libraries(nocturne_paint_sampling PRIVATE Nocturne::nocturne)
    add_test(NAME paint_sampling COMMAND nocturne_paint_sampling)
    add_executable(nocturne_surface tests/surface.c)
    target_link_libraries(nocturne_surface PRIVATE Nocturne::nocturne)
    add_test(NAME surface COMMAND nocturne_surface)
    add_executable(nocturne_blend tests/blend.c)
    target_link_libraries(nocturne_blend PRIVATE Nocturne::nocturne)
    add_test(NAME blend COMMAND nocturne_blend)
    add_executable(nocturne_canvas tests/canvas.c)
    target_link_libraries(nocturne_canvas PRIVATE Nocturne::nocturne)
    add_test(NAME canvas COMMAND nocturne_canvas)
    add_executable(nocturne_presentation tests/presentation.c)
    target_link_libraries(nocturne_presentation PRIVATE Nocturne::nocturne)
    add_test(NAME presentation COMMAND nocturne_presentation)
    add_executable(nocturne_component_lifecycle tests/component_lifecycle.c)
    target_link_libraries(nocturne_component_lifecycle PRIVATE Nocturne::nocturne)
    if(Threads_FOUND AND CMAKE_USE_PTHREADS_INIT)
        target_compile_definitions(nocturne_component_lifecycle PRIVATE NOCTURNE_TEST_PTHREAD)
        target_link_libraries(nocturne_component_lifecycle PRIVATE Threads::Threads)
    endif()
    add_test(NAME component_lifecycle COMMAND nocturne_component_lifecycle)
    add_executable(nocturne_box_layout tests/box_layout.c)
    target_link_libraries(nocturne_box_layout PRIVATE Nocturne::nocturne)
    add_test(NAME box_layout COMMAND nocturne_box_layout)
    add_executable(nocturne_viewport tests/consumer/viewport.c)
    target_link_libraries(nocturne_viewport PRIVATE Nocturne::nocturne)
    add_test(NAME viewport COMMAND nocturne_viewport)
    add_executable(nocturne_composition tests/consumer/composition.c tests/consumer/indicator.c)
    target_link_libraries(nocturne_composition PRIVATE Nocturne::nocturne)
    add_test(NAME composition COMMAND nocturne_composition)
    add_executable(nocturne_input tests/consumer/input.c)
    target_link_libraries(nocturne_input PRIVATE Nocturne::nocturne)
    add_test(NAME input COMMAND nocturne_input)
    add_executable(nocturne_routing tests/consumer/routing.c)
    target_link_libraries(nocturne_routing PRIVATE Nocturne::nocturne)
    add_test(NAME routing COMMAND nocturne_routing)
    add_executable(nocturne_editor tests/consumer/editor.c)
    target_link_libraries(nocturne_editor PRIVATE Nocturne::nocturne)
    add_test(NAME editor COMMAND nocturne_editor)
    add_executable(nocturne_editor_history tests/consumer/editor_history.c)
    target_link_libraries(nocturne_editor_history PRIVATE Nocturne::nocturne)
    add_test(NAME editor_history COMMAND nocturne_editor_history)
    add_executable(nocturne_controls tests/consumer/controls.c)
    target_link_libraries(nocturne_controls PRIVATE Nocturne::nocturne)
    add_test(NAME controls COMMAND nocturne_controls)
    if(TARGET Nocturne::windows_host)
        add_executable(nocturne_windows_console tests/consumer/windows_host.c)
        target_link_libraries(nocturne_windows_console PRIVATE Nocturne::windows_host)
        add_test(NAME windows_console COMMAND nocturne_windows_console)
    endif()
    if(TARGET Nocturne::posix_host)
        find_package(Python3 QUIET COMPONENTS Interpreter)
        if(Python3_Interpreter_FOUND)
            add_executable(nocturne_posix_probe tests/consumer/posix_host.c)
            target_link_libraries(nocturne_posix_probe PRIVATE Nocturne::posix_host)
            add_test(NAME posix_terminal COMMAND "${Python3_EXECUTABLE}"
                     "${CMAKE_CURRENT_SOURCE_DIR}/tests/consumer/posix_host.py" $<TARGET_FILE:nocturne_posix_probe>)
            add_executable(nocturne_window_terminal tests/consumer/window_terminal.c)
            target_link_libraries(nocturne_window_terminal PRIVATE Nocturne::posix_host)
            add_test(NAME window_terminal COMMAND "${Python3_EXECUTABLE}"
                     "${CMAKE_CURRENT_SOURCE_DIR}/tests/consumer/window_terminal.py" $<TARGET_FILE:nocturne_window_terminal>)
            add_executable(nocturne_panel_editor tests/consumer/panel_editor.c)
            target_link_libraries(nocturne_panel_editor PRIVATE Nocturne::posix_host)
            add_test(NAME panel_editor COMMAND "${Python3_EXECUTABLE}"
                     "${CMAKE_CURRENT_SOURCE_DIR}/tests/consumer/panel_editor.py" $<TARGET_FILE:nocturne_panel_editor>)
            if(TARGET Nocturne::graph)
                add_executable(nocturne_visual_terminal tests/consumer/visual_terminal.c)
                target_link_libraries(nocturne_visual_terminal PRIVATE Nocturne::posix_host Nocturne::graph)
                add_test(NAME visual_terminal COMMAND "${Python3_EXECUTABLE}"
                         "${CMAKE_CURRENT_SOURCE_DIR}/tests/consumer/visual_terminal.py" $<TARGET_FILE:nocturne_visual_terminal>)
            endif()
            add_executable(nocturne_terminal_profile tests/consumer/terminal_profile.c)
            target_link_libraries(nocturne_terminal_profile PRIVATE Nocturne::posix_host)
            add_test(NAME terminal_profiles COMMAND "${Python3_EXECUTABLE}"
                     "${CMAKE_CURRENT_SOURCE_DIR}/tests/consumer/terminal_profile.py"
                     $<TARGET_FILE:nocturne_terminal_profile>)
            find_program(NOCTURNE_TEST_TMUX tmux)
            if(NOCTURNE_TEST_TMUX)
                add_test(NAME terminal_profiles_tmux COMMAND "${Python3_EXECUTABLE}"
                         "${CMAKE_CURRENT_SOURCE_DIR}/tests/consumer/terminal_profile.py"
                         $<TARGET_FILE:nocturne_terminal_profile> "${NOCTURNE_TEST_TMUX}")
            endif()
        endif()
    endif()
endif()
if(NOCTURNE_BUILD_EXAMPLES)
    add_executable(nocturne_motion examples/motion.c)
    target_link_libraries(nocturne_motion PRIVATE Nocturne::nocturne)
endif()

install(TARGETS nocturne EXPORT NocturneTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        PATTERN "posix_host.h" EXCLUDE
        PATTERN "windows_host.h" EXCLUDE
        PATTERN "graph.h" EXCLUDE
        PATTERN "spatial.h" EXCLUDE
        PATTERN "spatial_mesh.h" EXCLUDE
        PATTERN "spatial_surface.h" EXCLUDE
        PATTERN "simulation.h" EXCLUDE)
install(EXPORT NocturneTargets NAMESPACE Nocturne:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Nocturne)
configure_package_config_file(cmake/NocturneConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/NocturneConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Nocturne)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/NocturneConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/NocturneConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/NocturneConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Nocturne)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/nocturne)
install(FILES unicode/17.0.0/LICENSE.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/nocturne
        RENAME Unicode-License.txt)
