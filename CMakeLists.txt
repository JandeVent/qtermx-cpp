cmake_minimum_required(VERSION 3.16)

project(qtermx-cpp VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Test)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        src/gui/input_encoder.cpp
        src/gui/renderer.cpp
        src/gui/terminal_widget.cpp
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(qtermx-cpp
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET qtermx-cpp APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(qtermx-cpp SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(qtermx-cpp
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(qtermx-cpp PRIVATE Qt${QT_VERSION_MAJOR}::Widgets qtermx_core)
target_include_directories(qtermx-cpp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/src/gui)

# Qt-free core library (parser, dispatcher, emulator, screen, params,
# utf8, wcwidth, pty, session) — headless-testable, zero Qt dependencies.
add_library(qtermx_core STATIC
    src/emulator.cpp
    src/parser.cpp
    src/pty.cpp
    src/screen.cpp
    src/session.cpp
    src/utf8_decoder.cpp
    src/wcwidth.cpp
)
target_include_directories(qtermx_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.qtermx-cpp)
endif()
set_target_properties(qtermx-cpp PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS qtermx-cpp
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(qtermx-cpp)
endif()

# Tests (core harness + QTest) — see tests/CMakeLists.txt
enable_testing()
add_subdirectory(tests)
