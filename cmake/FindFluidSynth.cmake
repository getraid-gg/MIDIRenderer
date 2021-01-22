# - Try to find FluidSynth
# 
# Once done this will define
#
#  FLUIDSYNTH_FOUND - system has libfluidsynth
#  FLUIDSYNTH_INCLUDE_DIRS - the libfluidsynth include directory
#  FLUIDSYNTH_LIBRARIES - Link these to use libfluidsynth
#  FLUIDSYNTH_VERSION_MAJOR
#
#  Users may define FLUIDSYNTH_ROOT to help find this library

if (FLUIDSYNTH_LIBRARIES AND FLUIDSYNTH_INCLUDE_DIRS)
  # in cache already
  set(FLUIDSYNTH_FOUND TRUE)
else (FLUIDSYNTH_LIBRARIES AND FLUIDSYNTH_INCLUDE_DIRS)
  include (FindPackageHandleStandardArgs)
  find_path(FLUIDSYNTH_INCLUDE_DIR
    NAMES
      fluidsynth.h
    PATHS
      /usr/include
      /usr/local/include
      /opt/local/include
      /sw/include
	  ${FLUIDSYNTH_ROOT}/include
  )
  
  find_library(FLUIDSYNTH_LIBRARY
    NAMES
      fluidsynth
    PATHS
      /usr/lib
      /usr/local/lib
      /opt/local/lib
      /sw/lib
	  ${FLUIDSYNTH_ROOT}/lib
  )

  set(FLUIDSYNTH_INCLUDE_DIRS
    ${FLUIDSYNTH_INCLUDE_DIR}
  )
  set(FLUIDSYNTH_LIBRARIES
    ${FLUIDSYNTH_LIBRARY}
  )

  if (FLUIDSYNTH_INCLUDE_DIRS AND FLUIDSYNTH_LIBRARIES)
    set(FLUIDSYNTH_FOUND TRUE)
  endif (FLUIDSYNTH_INCLUDE_DIRS AND FLUIDSYNTH_LIBRARIES)

  if (FLUIDSYNTH_FOUND)
    if (NOT FluidSynth_FIND_QUIETLY)
      message(STATUS "Found libfluidsynth: ${FLUIDSYNTH_LIBRARIES}")
    endif (NOT FluidSynth_FIND_QUIETLY)

    find_file(_FluidSynth_VERSION_HEADER version.h PATHS ${FLUIDSYNTH_INCLUDE_DIR}/fluidsynth)
    if (${_FluidSynth_VERSION_HEADER} STREQUAL "_FluidSynth_VERSION_HEADER-NOTFOUND")
      message(FATAL_ERROR "Couldn't find FluidSynth version header at ${FLUIDSYNTH_INCLUDE_DIR}/fluidsynth/version.h")
    else()
      file(READ ${_FluidSynth_VERSION_HEADER} _FluidSynth_VERSION_HEADER_CONTENTS)

      string(REGEX MATCH "FLUIDSYNTH_VERSION_MAJOR[\t ]+([0-9]+)" _ "${_FluidSynth_VERSION_HEADER_CONTENTS}")
      set(FLUIDSYNTH_VERSION_MAJOR ${CMAKE_MATCH_1})
      string(REGEX MATCH "FLUIDSYNTH_VERSION_MINOR[\t ]+([0-9]+)" _ "${_FluidSynth_VERSION_HEADER_CONTENTS}")
      set(FLUIDSYNTH_VERSION_MINOR ${CMAKE_MATCH_1})
      string(REGEX MATCH "FLUIDSYNTH_VERSION_MICRO[\t ]+([0-9]+)" _ "${_FluidSynth_VERSION_HEADER_CONTENTS}")
      set(FLUIDSYNTH_VERSION_MICRO ${CMAKE_MATCH_1})

      set(FLUIDSYNTH_VERSION ${FLUIDSYNTH_VERSION_MAJOR}.${FLUIDSYNTH_VERSION_MINOR}.${FLUIDSYNTH_VERSION_MICRO})
    endif()

  else (FLUIDSYNTH_FOUND)
    if (FluidSynth_FIND_REQUIRED)
      message(FATAL_ERROR "Could not find libfluidsynth")
    endif (FluidSynth_FIND_REQUIRED)
  endif (FLUIDSYNTH_FOUND)

  # show the FLUIDSYNTH_INCLUDE_DIRS and FLUIDSYNTH_LIBRARIES variables only in the advanced view
  mark_as_advanced(FLUIDSYNTH_INCLUDE_DIRS FLUIDSYNTH_LIBRARIES)

endif (FLUIDSYNTH_LIBRARIES AND FLUIDSYNTH_INCLUDE_DIRS)

find_package_handle_standard_args(FluidSynth
  REQUIRED_VARS
    FLUIDSYNTH_LIBRARY
	FLUIDSYNTH_INCLUDE_DIR
	FLUIDSYNTH_VERSION
  VERSION_VAR
    FLUIDSYNTH_VERSION)
