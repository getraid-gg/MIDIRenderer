cmake_minimum_required(VERSION 3.16)
project(midirenderer VERSION 1.0.0)
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
set(COPY_DLLS_SCRIPT "${CMAKE_BINARY_DIR}/copy_dlls.cmake")
list(APPEND EXECUTABLES
	midirenderer)
configure_file(copy_dlls.cmake.in ${COPY_DLLS_SCRIPT} @ONLY)
add_custom_command(TARGET midirenderer POST_BUILD
	COMMAND ${CMAKE_COMMAND} -DCONFIG_NAME=$<CONFIG> -P ${COPY_DLLS_SCRIPT})
endif()

install(TARGETS midirenderer
	RUNTIME DESTINATION bin
	COMPONENT midirenderer)

if (WIN32)
install(DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/"
	DESTINATION bin
	COMPONENT midirenderer
	FILES_MATCHING PATTERN "*.dll")
endif (WIN32)

install(DIRECTORY "${CMAKE_SOURCE_DIR}/licenses"
	DESTINATION .
	COMPONENT packaging EXCLUDE_FROM_ALL)

set(CPACK_PACKAGING_INSTALL_PREFIX "/")
set(CPACK_INSTALL_CMAKE_PROJECTS
	"${CMAKE_CURRENT_BINARY_DIR};${CMAKE_PROJECT_NAME};midirenderer;/"
	"${CMAKE_CURRENT_BINARY_DIR};${CMAKE_PROJECT_NAME};packaging;/")
set(CPACK_PACKAGE_VENDOR "getraid-gg")
set(CPACK_PACKAGE_DESCRIPTION "A midi to RPGMV-compatible looping OGG converter")
set(CPACK_GENERATOR "ZIP")
include(CPack)
