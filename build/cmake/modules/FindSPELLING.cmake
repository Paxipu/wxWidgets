# - Try to find libspelling
# Once done, this will define
#
#  SPELLING_FOUND - system has libspelling
#  SPELLING_INCLUDE_DIRS - The include directory to use for the libspelling headers
#  SPELLING_LIBRARIES - Link these to use libspelling
#
# libspelling is the GTK4 counterpart of gspell: gspell is a GTK+ 3 library and
# linking it into a GTK4 build would put libgtk-3 and libgtk-4 in the same
# process, which GTK4 refuses to run.  See the SPELLCHECK section of
# configure.ac, which makes the same distinction.

find_package(PkgConfig)
pkg_check_modules(PC_SPELLING QUIET libspelling-1)

find_path(SPELLING_INCLUDE_DIRS
    NAMES libspelling.h
    HINTS ${PC_SPELLING_INCLUDEDIR}
          ${PC_SPELLING_INCLUDE_DIRS}
    PATH_SUFFIXES libspelling-1
)

find_library(SPELLING_LIBRARIES
    NAMES spelling-1
    HINTS ${PC_SPELLING_LIBDIR}
          ${PC_SPELLING_LIBRARY_DIRS}
)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(SPELLING
                                  REQUIRED_VARS SPELLING_INCLUDE_DIRS SPELLING_LIBRARIES
                                  VERSION_VAR   PC_SPELLING_VERSION)

if(SPELLING_FOUND AND PC_SPELLING_INCLUDE_DIRS)
    # libspelling.h reaches into gtksourceview's headers, so that dependency's
    # include directory has to come along too.  pkg-config resolves it through
    # Requires:; find_path() above only ever locates libspelling's own.
    list(APPEND SPELLING_INCLUDE_DIRS ${PC_SPELLING_INCLUDE_DIRS})
    list(REMOVE_DUPLICATES SPELLING_INCLUDE_DIRS)
endif()

mark_as_advanced(
    SPELLING_INCLUDE_DIRS
    SPELLING_LIBRARIES
)
