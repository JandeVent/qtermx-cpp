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