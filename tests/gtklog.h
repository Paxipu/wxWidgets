///////////////////////////////////////////////////////////////////////////////
// Name:        tests/gtklog.h
// Purpose:     Counting the GTK log messages a test provokes.
// Copyright:   (c) 2026 wxWidgets development team
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_TESTS_GTKLOG_H_
#define _WX_TESTS_GTKLOG_H_

#ifdef __WXGTK__

// How many GTK log messages the process has seen so far.
//
// tests/test.cpp already intercepts them, to say which test they arrived
// under; this makes the count available, so that a test can be about not
// provoking one. That is worth having because GTK's own log is the only thing
// some faults say anything to: it complains that an invariant of its own is
// broken, wx carries on, and the run passes.
//
// Only useful where the message arrives at a predictable point -- an assertion
// GTK makes while tearing a widget down, say. Several messages appear only in
// a full-suite run and not when the same code is exercised alone, and this
// cannot see those.
unsigned wxTestGetGTKLogCount();

#endif // __WXGTK__

#endif // _WX_TESTS_GTKLOG_H_
