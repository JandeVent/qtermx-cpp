# Test executables. Core tests are Qt-free (plain C++ + harness.h);
# GUI tests use Qt Test (QTest). See docs/adr/0001-test-harness.md.

# --- Qt-free core tests -----------------------------------------------------
add_executable(core_tests
    core/test_main.cpp
    core/test_harness_smoke.cpp
    core/test_fixture_corpus.cpp
    core/test_parser_ground.cpp
    core/test_parser_csi.cpp
    core/test_parser_escape.cpp
    core/test_parser_dcs.cpp
    core/test_parser_osc.cpp
    core/test_parser_states.cpp
    core/test_parser_hardening.cpp
    core/test_screen.cpp
    core/test_conformance.cpp
    core/test_wcwidth.cpp
    core/test_emulator_dispatch.cpp
    core/test_screen_motion.cpp
    core/test_screen_region.cpp
    core/test_screen_erase.cpp
    core/test_screen_scroll.cpp
    core/test_screen_sgr.cpp
    core/test_screen_modes.cpp
    core/test_screen_mode_behaviors.cpp
    core/test_screen_keyboard_modes.cpp
    core/test_screen_tabs.cpp
    core/test_screen_save_restore.cpp
    core/test_screen_charsets.cpp
    core/test_screen_wrapped.cpp
    core/test_screen_alt_screen.cpp
    core/test_screen_decaln.cpp
    core/test_screen_effective_rendition.cpp
    core/test_screen_truecolor.cpp
    core/test_palette.cpp
    core/test_screen_scrollback.cpp
    core/test_pty.cpp
    core/test_selection.cpp
    core/test_session.cpp
    core/test_emulator_osc_color.cpp
)
target_include_directories(core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_link_libraries(core_tests PRIVATE qtermx_core)
add_test(NAME core_tests COMMAND core_tests)

# --- Qt layer tests (QTest) -------------------------------------------------
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)

add_executable(gui_tests
    gui/test_smoke.cpp
)
target_link_libraries(gui_tests PRIVATE Qt${QT_VERSION_MAJOR}::Test)
add_test(NAME gui_tests COMMAND gui_tests)

add_executable(gui_input_tests
    gui/test_input.cpp
    ../src/gui/input_encoder.cpp
)
target_include_directories(gui_input_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gui ../src/gui)
target_link_libraries(gui_input_tests PRIVATE Qt${QT_VERSION_MAJOR}::Test Qt${QT_VERSION_MAJOR}::Gui)
add_test(NAME gui_input_tests COMMAND gui_input_tests)

add_executable(gui_render_tests
    gui/test_render.cpp
    ../src/gui/renderer.cpp
    ../src/utf8_decoder.cpp
)
target_include_directories(gui_render_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gui ../src/gui ../src)
target_link_libraries(gui_render_tests PRIVATE Qt${QT_VERSION_MAJOR}::Test Qt${QT_VERSION_MAJOR}::Gui)
add_test(NAME gui_render_tests COMMAND gui_render_tests)

add_executable(gui_widget_tests
    gui/test_widget.cpp
    ../src/gui/terminal_widget.cpp
    ../src/gui/input_encoder.cpp
    ../src/gui/renderer.cpp
    ../src/session.cpp
    ../src/pty.cpp
    ../src/screen.cpp
    ../src/emulator.cpp
    ../src/parser.cpp
    ../src/utf8_decoder.cpp
    ../src/wcwidth.cpp
)
target_include_directories(gui_widget_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gui ../src/gui ../src)
target_link_libraries(gui_widget_tests PRIVATE Qt${QT_VERSION_MAJOR}::Test Qt${QT_VERSION_MAJOR}::Widgets)
add_test(NAME gui_widget_tests COMMAND gui_widget_tests)
# Offscreen: the Python oracle's widget tests run offscreen too — the
# backing image is dpr-scaled, and the assertions assume dpr == 1.
set_tests_properties(gui_widget_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")