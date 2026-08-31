# - Try to find LibNotify
# This module defines the following variables:
#
#  LIBNOTIFY_FOUND - LibNotify was found
#  LIBNOTIFY_INCLUDE_DIRS - the LibNotify include directories
#  LIBNOTIFY_LIBRARIES - link these to use LibNotify
#
# Copyright (C) 2012 Raphael Kubo da Costa <rakuco@webkit.org>
# Copyright (C) 2014 Collabora Ltd.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1.  Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
# 2.  Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND ITS CONTRIBUTORS ``AS
# IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR ITS
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

find_package(PkgConfig)
pkg_check_modules(LIBNOTIFY QUIET libnotify)

find_path(LIBNOTIFY_INCLUDE_DIRS
    NAMES notify.h
    HINTS ${LIBNOTIFY_INCLUDEDIR}
          ${LIBNOTIFY_INCLUDE_DIRS}
    PATH_SUFFIXES libnotify
)

find_library(LIBNOTIFY_LIBRARIES
    NAMES notify
    HINTS ${LIBNOTIFY_LIBDIR}
          ${LIBNOTIFY_LIBRARY_DIRS}
)

# pkg_check_modules() above sets LIBNOTIFY_VERSION, but the find_path() and
# find_library() calls do not: they can succeed on their own when libnotify is
# installed but its .pc file is not on the pkg-config search path.  The version
# then stays empty, "" VERSION_LESS "0.7" is true, and the caller quietly
# concludes the library is older than 0.7 -- which builds the pre-0.7 call to
# notify_notification_new() against a modern libnotify and fails to compile.
# Read the version out of the header when pkg-config could not supply it.
if(LIBNOTIFY_INCLUDE_DIRS AND NOT LIBNOTIFY_VERSION)
    set(_libnotify_features "${LIBNOTIFY_INCLUDE_DIRS}/notify-features.h")
    if(EXISTS "${_libnotify_features}")
        file(STRINGS "${_libnotify_features}" _libnotify_version_lines
             REGEX "^#define[ \t]+NOTIFY_VERSION_(MAJOR|MINOR|MICRO)[ \t]+\\(-?[0-9]+\\)")
        foreach(_part MAJOR MINOR MICRO)
            foreach(_line ${_libnotify_version_lines})
                if(_line MATCHES "NOTIFY_VERSION_${_part}[ \t]+\\(([0-9]+)\\)")
                    set(_libnotify_${_part} "${CMAKE_MATCH_1}")
                endif()
            endforeach()
        endforeach()
        if(DEFINED _libnotify_MAJOR AND DEFINED _libnotify_MINOR AND DEFINED _libnotify_MICRO)
            set(LIBNOTIFY_VERSION "${_libnotify_MAJOR}.${_libnotify_MINOR}.${_libnotify_MICRO}")
        endif()
        unset(_libnotify_MAJOR)
        unset(_libnotify_MINOR)
        unset(_libnotify_MICRO)
        unset(_libnotify_version_lines)
    endif()
    unset(_libnotify_features)
endif()

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(LIBNOTIFY REQUIRED_VARS LIBNOTIFY_INCLUDE_DIRS LIBNOTIFY_LIBRARIES
                                            VERSION_VAR   LIBNOTIFY_VERSION)

mark_as_advanced(
    LIBNOTIFY_INCLUDE_DIRS
    LIBNOTIFY_LIBRARIES
)
