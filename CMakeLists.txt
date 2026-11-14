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

project(Neatpad LANGUAGES C CXX)

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
