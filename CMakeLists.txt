#
#	Neatpad's portable parts: the TextDocument core and its tests.
#
#	Visual Studio's Neatpad.sln builds the Win32 app. This builds what isn't
#	tied to one platform, on Windows and on Linux:
#
#		cmake -S . -B build/cmake
#		cmake --build build/cmake --config Release
#		ctest --test-dir build/cmake -C Release
#
#	The source lists repeat those in the .vcxproj files.
#
cmake_minimum_required(VERSION 3.16)

# The version is the Win32 app's too (resource.rc), which release.yml stamps
# from the tag
project(Neatpad VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # As the Visual Studio projects build: warning level 3, Unicode
    add_compile_options(/W3)
    add_compile_definitions(UNICODE _UNICODE)
endif()

# Storage and the text model
add_library(TextDocument STATIC
    src/TextDocument/SaveFile.cpp
    src/TextDocument/sequence.cpp
    src/TextDocument/sequence_buf.cpp
    src/TextDocument/sequence_chars.cpp
    src/TextDocument/sequence_diff.cpp
    src/TextDocument/sequence_lines.cpp
    src/TextDocument/sequence_state.cpp
    src/TextDocument/TextDocument.cpp
    src/TextDocument/TextDocumentCoord.cpp
    src/TextDocument/TextDocumentFind.cpp
    src/TextDocument/Unicode.c
)

target_include_directories(TextDocument PUBLIC src/TextDocument)

# The TextView engine: the editor, without its platform's window
add_library(TextViewEngine STATIC
    src/TextView/TextView.cpp
    src/TextView/TextViewClipboard.cpp
    src/TextView/TextViewFile.cpp
    src/TextView/TextViewFilter.cpp
    src/TextView/TextViewFont.cpp
    src/TextView/TextViewKeyInput.cpp
    src/TextView/TextViewKeyNav.cpp
    src/TextView/TextViewMouse.cpp
    src/TextView/TextViewPaint.cpp
    src/TextView/TextViewRecovery.cpp
    src/TextView/TextViewScroll.cpp
    src/TextView/TextViewSyntax.cpp
)

target_link_libraries(TextViewEngine PUBLIC TextDocument)

# The GTK view, NeatpadTextView, and a window to try it in: where GTK 4 is
find_package(PkgConfig QUIET)

if(PkgConfig_FOUND)
    pkg_check_modules(GTK4 IMPORTED_TARGET gtk4)
    pkg_check_modules(ADWAITA IMPORTED_TARGET libadwaita-1)
endif()

if(GTK4_FOUND)
    add_library(TextViewGtk STATIC
        src/TextView/gtk/GtkCanvas.cpp
        src/TextView/gtk/NeatpadTextView.cpp
        src/TextView/gtk/PangoTextLayout.cpp
        src/TextView/gtk/TextViewGtk.cpp
    )

    target_include_directories(TextViewGtk PUBLIC src/TextView/gtk)
    target_link_libraries(TextViewGtk PUBLIC TextViewEngine PkgConfig::GTK4)

    add_executable(textview-demo tests/gtk/textview_demo.c)
    target_link_libraries(textview-demo PRIVATE TextViewGtk)

    # These need a display: without one they're skipped
    add_executable(GtkTests tests/gtk/gtk_tests.cpp tests/test_helpers.cpp src/Neatpad/gtk/recovery.c)
    target_link_libraries(GtkTests PRIVATE TextViewGtk)

    # The app, where libadwaita is too
    if(ADWAITA_FOUND)
        # its icons, compiled in as a GResource
        find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)
        set(NEATPAD_GTK ${CMAKE_CURRENT_SOURCE_DIR}/src/Neatpad/gtk)
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/neatpad-resources.c
            COMMAND ${GLIB_COMPILE_RESOURCES} --generate-source --sourcedir=${NEATPAD_GTK}
                    --target=${CMAKE_CURRENT_BINARY_DIR}/neatpad-resources.c ${NEATPAD_GTK}/neatpad.gresource.xml
            DEPENDS ${NEATPAD_GTK}/neatpad.gresource.xml ${NEATPAD_GTK}/icons/neatpad-highlight-symbolic.svg
                    ${NEATPAD_GTK}/icons/neatpad-filter-symbolic.svg)

        add_executable(neatpad src/Neatpad/gtk/neatpad.c src/Neatpad/gtk/palette.c src/Neatpad/gtk/preferences.c
                               src/Neatpad/gtk/recovery.c ${CMAKE_CURRENT_BINARY_DIR}/neatpad-resources.c)
        target_link_libraries(neatpad PRIVATE TextViewGtk PkgConfig::ADWAITA)
        target_compile_definitions(neatpad PRIVATE NEATPAD_VERSION="${PROJECT_VERSION}")
    endif()
endif()

# The portable tests: the sequence, line and character indexes, TextDocument,
# and the TextView engine with a fake host
add_executable(CoreTests
    tests/main.cpp
    tests/test_helpers.cpp
    tests/sequence_tests.cpp
    tests/line_tests.cpp
    tests/char_tests.cpp
    tests/document_tests.cpp
    tests/engine_tests.cpp
)

target_link_libraries(CoreTests PRIVATE TextDocument TextViewEngine)

enable_testing()
add_test(NAME CoreTests COMMAND CoreTests)

if(GTK4_FOUND)
    add_test(NAME GtkTests COMMAND GtkTests)
    set_tests_properties(GtkTests PROPERTIES SKIP_RETURN_CODE 77)
endif()
