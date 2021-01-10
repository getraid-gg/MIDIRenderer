cmake_minimum_required(VERSION 3.16)
project(midirenderer)
set(CMAKE_CXX_STANDARD 17)

set(MSVC_USE_STATIC_RUNTIME_LIBRARY TRUE CACHE BOOL "Whether to use the static MSVC runtime or the runtime DLLs")
macro(set_static_runtime _executable)
set_property(TARGET ${_executable} PROPERTY MSVC_RUNTIME_LIBRARY
	"MultiThreaded$<$<CONFIG:Debug>:Debug>$<$<NOT:$<BOOL:${MSVC_USE_STATIC_RUNTIME_LIBRARY}>>:DLL>")
endmacro()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
find_package(FluidSynth REQUIRED)
find_package(Ogg REQUIRED)
find_package(Vorbis REQUIRED)

list(APPEND MIDIRENDERER_THIRD_PARTY_SRC
	src/cxxopts.hpp)

list(APPEND MIDIRENDERER_SRC
	${MIDIRENDERER_THIRD_PARTY_SRC}
	src/platformchar.h
	src/platformsupport.h
	src/platformargswrapper.h
	src/pathresolution.h
	src/oggvorbisencoder.h
	src/midivorbisrenderer.h
	src/platformsupport.cpp
	src/platformargswrapper.cpp
	src/pathresolution.cpp
	src/oggvorbisencoder.cpp
	src/midivorbisrenderer.cpp
	src/midirenderer.cpp)

add_executable(midirenderer "${MIDIRENDERER_SRC}")
target_include_directories(midirenderer PRIVATE
	${FLUIDSYNTH_INCLUDE_DIR}
	${Vorbis_Vorbis_INCLUDE_DIRS}
	${Vorbis_Enc_INCLUDE_DIRS})

target_link_libraries(midirenderer PRIVATE
	${FLUIDSYNTH_LIBRARY}
	Vorbis::vorbis
	Vorbis::vorbisenc)

if (WIN32 AND (FLUIDSYNTH_VERSION_MAJOR LESS 3))
	message(STATUS "FluidSynth version is less than 3.0.0; early checking for valid SoundFont and MIDI files is disabled on Windows and SoundFont paths may not contain UTF-16 characters")
endif()

set_static_runtime(midirenderer)

if (WIN32)
set(INSTALL_DLLS_SCRIPT "${CMAKE_BINARY_DIR}/install_dlls.cmake")
list(APPEND EXECUTABLES
    midirenderer)
configure_file(install_dlls.cmake.in ${INSTALL_DLLS_SCRIPT} @ONLY)
install(SCRIPT ${INSTALL_DLLS_SCRIPT})
endif()