///////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/evtloop.h
// Purpose:     wxGTK event loop implementation
// Author:      Vadim Zeitlin
// Created:     2008-12-27
// Copyright:   (c) 2008 Vadim Zeitlin <vadim@wxwidgets.org>
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GTK_EVTLOOP_H_
#define _WX_GTK_EVTLOOP_H_

// ----------------------------------------------------------------------------
// wxGUIEventLoop for wxGTK
// ----------------------------------------------------------------------------

#ifdef __WXGTK4__
typedef struct _GdkEvent        GdkEvent;
#else
typedef union  _GdkEvent        GdkEvent;
#endif

#include <vector>

class WXDLLIMPEXP_CORE wxGUIEventLoop : public wxEventLoopBase
{
public:
    wxGUIEventLoop();
    virtual ~wxGUIEventLoop();

    virtual bool Pending() const override;
    virtual bool Dispatch() override;
    virtual int DispatchTimeout(unsigned long timeout) override;
    virtual void WakeUp() override;

    // implementation only from now on

#ifndef __WXGTK4__
    // Neither of these has any GTK4 use. GTK4 removed gdk_event_handler_set(),
    // the hook DoYieldFor() installed to sort native events by category and
    // defer the ones not wanted yet, so there is nothing to store; and
    // EventAlreadyProcessed(), the only caller of the second, is itself GTK3
    // only, GdkEvent being opaque under GTK4 and so not comparable byte-wise.
    void StoreGdkEventForLaterProcessing(GdkEvent* ev)
        { m_queuedGdkEvents.push_back(ev); }

    // Check if this event is the same as the last event passed to this
    // function and store it for future checks.
    bool GTKIsSameAsLastEvent(const GdkEvent* ev, size_t size);
#endif // !__WXGTK4__

protected:
    virtual int DoRun() override;
    virtual void DoStop(int rc) override;
    virtual void DoYieldFor(long eventsToProcess) override;

private:
    // the exit code of this event loop
    int m_exitcode;

#ifdef __WXGTK4__
    // GTK4 has no gtk_main(): running a loop means running a GMainLoop, and
    // this is ours while it is running and null otherwise.
    GMainLoop* m_mainLoop;
#else
    // used to temporarily store events processed in DoYieldFor()
    std::vector<GdkEvent*> m_queuedGdkEvents;

    // last event passed to GTKIsSameAsLastEvent()
    GdkEvent* m_lastEvent;
#endif

    wxDECLARE_NO_COPY_CLASS(wxGUIEventLoop);
};

#endif // _WX_GTK_EVTLOOP_H_
