cmake_minimum_required(VERSION 3.20)
project(MindfulCompute LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Version / build stamp ----
# Replicates build.sh's scheme: a fixed version plus a timestamped build number
# (yyMMdd.HHmm, local time) so a stale running copy is obvious at a glance. The
# stamp is captured HERE, at CONFIGURE time -- so re-running cmake (which
# build.cmd does on every build) refreshes it; a plain `ninja` re-link does not.
string(TIMESTAMP MINDFUL_BUILD_STAMP "%y%m%d.%H%M")
set(MINDFUL_VERSION "1.2.2-win")

# ---- Embedded resources (app icon + bowl wav) ----
# Absolute paths are baked into a configured .rc so rc.exe resolves them from any
# build dir. Icon: first ICON resource -> also the exe's Explorer icon. Wav:
# WAVE resource -> PlaySound(SND_RESOURCE), making the shipped exe a single file.
set(APP_ICO_PATH  "${CMAKE_CURRENT_SOURCE_DIR}/assets/app.ico")
set(BOWL_WAV_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../assets/sounds/tibetan_bowl.wav")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/resources.rc.in
               ${CMAKE_CURRENT_BINARY_DIR}/resources.rc @ONLY)

add_executable(MindfulCompute WIN32
    src/main.cpp
    src/Graphics.cpp
    src/OverlayWindow.cpp
    src/DimWindow.cpp
    src/DimManager.cpp
    src/TrayIcon.cpp
    src/TextField.cpp
    src/Text.cpp
    src/Session.cpp
    src/Journal.cpp
    src/Sound.cpp
    src/LoginItem.cpp
    src/Log.cpp
    src/CrashDump.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/resources.rc   # icon + bowl wav, embedded
    app.manifest   # CMake feeds this to mt.exe -> embedded (per-monitor-v2 DPI)
)

# rc.exe needs src/ on its include path to resolve #include "resource.h".
target_include_directories(MindfulCompute PRIVATE src)

target_compile_definitions(MindfulCompute PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    MINDFUL_VERSION="${MINDFUL_VERSION}"
    MINDFUL_BUILD_STAMP="${MINDFUL_BUILD_STAMP}"
)

target_compile_options(MindfulCompute PRIVATE /W4 /permissive- /utf-8 /EHsc)

# ---- Symbolication for the optimized Release build ----
# RelWithDebInfo (the dev build) already carries /Zi + /DEBUG. Give the optimized
# Release the same so a crash dump from the laptop can be symbolized back here
# against MindfulCompute.pdb -- WITHOUT changing the shipped exe: /DEBUG only adds
# a tiny debug-directory entry pointing at the (separate) .pdb, and we re-assert
# /OPT:REF,ICF (which /DEBUG otherwise turns off) so the exe stays byte-for-byte
# the same size it was. The .pdb never ships unless the user copies it.
target_compile_options(MindfulCompute PRIVATE $<$<CONFIG:Release>:/Zi>)
target_link_options(MindfulCompute PRIVATE
    $<$<CONFIG:Release>:/DEBUG;/OPT:REF;/OPT:ICF>)

target_link_libraries(MindfulCompute PRIVATE
    d2d1
    dwrite
    dcomp
    dxgi
    d3d11
    windowscodecs
    wtsapi32     # WM_WTSSESSION_CHANGE (unlock)
    winmm        # PlaySound (bowl)
    shell32      # Shell_NotifyIcon, ShellExecute
    shlwapi
    comctl32     # themed EDIT + SetWindowSubclass
    shcore       # GetDpiForMonitor
    advapi32     # HKCU Run key (start at login)
    user32
    gdi32
)

# ---- Bundle the bowl next to the exe (resolved relative to the exe at runtime) ----
add_custom_command(TARGET MindfulCompute POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/../assets/sounds/tibetan_bowl.wav"
            "$<TARGET_FILE_DIR:MindfulCompute>/tibetan_bowl.wav"
    COMMENT "Copying tibetan_bowl.wav next to the exe"
)
