# - Try to find WebKitGTK for GTK 4
# Once done, this will define
#
#  WEBKITGTK6_FOUND - system has webkitgtk-6.0
#  WEBKITGTK6_INCLUDE_DIRS - The include directories to use for its headers
#  WEBKITGTK6_LIBRARIES - Link these to use it
#
# This is a different package from webkit2gtk, not a newer version of it: the
# GTK+ 3 WebKit headers name GtkContainer, GtkAction and GdkEvent* types that
# GTK4 does not have, so they cannot be compiled in a GTK4 build at all.  See
# the WEBVIEW section of configure.ac, which makes the same distinction.

find_package(PkgConfig)
pkg_check_modules(PC_WEBKITGTK6 QUIET webkitgtk-6.0)

find_path(WEBKITGTK6_INCLUDE_DIRS
    NAMES webkit/webkit.h
    HINTS ${PC_WEBKITGTK6_INCLUDEDIR}
          ${PC_WEBKITGTK6_INCLUDE_DIRS}
    PATH_SUFFIXES webkitgtk-6.0
)

find_library(WEBKITGTK6_LIBRARY
    NAMES webkitgtk-6.0
    HINTS ${PC_WEBKITGTK6_LIBDIR}
          ${PC_WEBKITGTK6_LIBRARY_DIRS}
)

find_library(WEBKITGTK6_JS_LIBRARY
    NAMES javascriptcoregtk-6.0
    HINTS ${PC_WEBKITGTK6_LIBDIR}
          ${PC_WEBKITGTK6_LIBRARY_DIRS}
)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(WEBKITGTK6
                                  REQUIRED_VARS WEBKITGTK6_INCLUDE_DIRS
                                                WEBKITGTK6_LIBRARY
                                  VERSION_VAR   PC_WEBKITGTK6_VERSION)

if(WEBKITGTK6_FOUND)
    set(WEBKITGTK6_LIBRARIES ${WEBKITGTK6_LIBRARY} ${WEBKITGTK6_JS_LIBRARY})

    # webkit/webkit.h includes GTK's and libsoup's headers, which pkg-config
    # resolves through Requires: and find_path() above does not: it only ever
    # locates webkitgtk's own directory.
    if(PC_WEBKITGTK6_INCLUDE_DIRS)
        list(APPEND WEBKITGTK6_INCLUDE_DIRS ${PC_WEBKITGTK6_INCLUDE_DIRS})
        list(REMOVE_DUPLICATES WEBKITGTK6_INCLUDE_DIRS)
    endif()
else()
    set(WEBKITGTK6_LIBRARIES)
endif()

mark_as_advanced(
    WEBKITGTK6_INCLUDE_DIRS
    WEBKITGTK6_LIBRARY
    WEBKITGTK6_JS_LIBRARY
    WEBKITGTK6_LIBRARIES
)
