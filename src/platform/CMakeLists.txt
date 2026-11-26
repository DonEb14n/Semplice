add_library(semplice_platform STATIC
    SingleInstance.h
    SingleInstance.cpp
    CrashHandler.h
    CrashHandler.cpp
    WindowsFileAttributes.h
    WindowsFileAttributes.cpp
    WindowsCodepageConverter.h
    WindowsCodepageConverter.cpp
    WindowsAtomicReplace.h
    WindowsAtomicReplace.cpp
    QtAudioBackend.h
    QtAudioBackend.cpp
    SharedReadFile.h
    SharedReadFile.cpp
    WinSmtc.h
    WinSmtc.cpp
    WinThumbBar.h
    WinThumbBar.cpp
    StallSampler.h
    StallSampler.cpp
    StartMenuShortcut.h
    StartMenuShortcut.cpp
    FramelessAnimation.h
    FramelessAnimation.cpp
)
target_include_directories(semplice_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(semplice_platform PUBLIC Qt6::Core Qt6::Gui Qt6::Network Qt6::Multimedia semplice_core)
if(WIN32)
    # WinRT activation (SMTC), COM, GDI for the thumbnail buttons
    target_link_libraries(semplice_platform PRIVATE runtimeobject ole32 uuid shell32 shcore shlwapi gdi32 user32 dbghelp)
endif()
semplice_set_warnings(semplice_platform)
# COM code passes interface pointers through void** (QueryInterface and friends)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(semplice_platform PRIVATE -fno-strict-aliasing)
endif()
