# TagLib (LGPL-2.1 / MPL-1.1): reads and writes the metadata of audio files.
# Pinned release tarball (it contains the bundled utfcpp, a plain git archive does not),
# built as a static library without zlib, bindings, examples and tests.
include(FetchContent)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_BINDINGS OFF CACHE BOOL "" FORCE)
set(WITH_ZLIB OFF CACHE BOOL "" FORCE)
set(WITH_DSF OFF CACHE BOOL "" FORCE)
set(WITH_MATROSKA OFF CACHE BOOL "" FORCE)
set(WITH_MOD OFF CACHE BOOL "" FORCE)
set(WITH_SHORTEN OFF CACHE BOOL "" FORCE)
set(WITH_TRUEAUDIO OFF CACHE BOOL "" FORCE)

FetchContent_Declare(taglib
    URL https://github.com/taglib/taglib/releases/download/v2.3.2/taglib-2.3.2.tar.gz
    URL_HASH SHA256=3ca2d8afaa7f1cf7f6ed10e511ebc368bfacd6dcaa3dbfa690b89e502e8963dc
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    EXCLUDE_FROM_ALL
    SYSTEM
)
FetchContent_MakeAvailable(taglib)


# TagLib does not export its header folders when built as a sub-project, so wrap it in our own
# interface target that carries them (as SYSTEM headers: no warnings from third-party code).
set(_taglib_dirs
    taglib taglib/toolkit taglib/ape taglib/asf taglib/flac taglib/mp4 taglib/mpeg taglib/mpeg/id3v1
    taglib/mpeg/id3v2 taglib/mpeg/id3v2/frames taglib/ogg taglib/ogg/flac taglib/ogg/vorbis
    taglib/riff taglib/riff/aiff taglib/riff/wav)
add_library(semplice_taglib INTERFACE)
target_link_libraries(semplice_taglib INTERFACE tag)
foreach(_dir ${_taglib_dirs})
    target_include_directories(semplice_taglib SYSTEM INTERFACE "${taglib_SOURCE_DIR}/${_dir}")
endforeach()
target_include_directories(semplice_taglib SYSTEM INTERFACE "${taglib_BINARY_DIR}")
