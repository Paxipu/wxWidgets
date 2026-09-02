///////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/private/webkit.h
// Purpose:     wxWebKitGtk RAII wrappers declaration
// Author:      Jose Lorenzo
// Created:     2017-08-21
// Copyright:   (c) 2017 Jose Lorenzo <josee.loren@gmail.com>
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GTK_PRIVATE_WEBKIT_H_
#define _WX_GTK_PRIVATE_WEBKIT_H_

#include "wx/buffer.h"

#ifdef __WXGTK4__
    // WebKitGTK has a separate API version for GTK4, with its own header, and
    // the deprecated JavaScriptCore C API used below is not part of it any
    // more: the only way to get at a script result is the JSC GObject API.
    #include <webkit/webkit.h>
#else
    #include <webkit2/webkit2.h>
    #include <JavaScriptCore/JSStringRef.h>
#endif

// ----------------------------------------------------------------------------
// The result of evaluating a script
//
// This is a WebKitJavascriptResult, which owns a JSValueRef, up to the GTK3
// API and a plain (reference counted) JSCValue under GTK4.
// ----------------------------------------------------------------------------

#ifdef __WXGTK4__
typedef JSCValue wxWebKitJSValue;
#else
typedef WebKitJavascriptResult wxWebKitJSValue;
#endif

// ----------------------------------------------------------------------------
// RAII wrapper of wxWebKitJSValue taking care of freeing it
// ----------------------------------------------------------------------------

class wxWebKitJavascriptResult
{
public:
    explicit wxWebKitJavascriptResult(wxWebKitJSValue *r)
        : m_jsresult(r)
    {
    }

    ~wxWebKitJavascriptResult()
    {
        if ( m_jsresult != nullptr )
        {
#ifdef __WXGTK4__
            g_object_unref(m_jsresult);
#else
            webkit_javascript_result_unref(m_jsresult);
#endif
        }
    }

    operator wxWebKitJSValue *() const { return m_jsresult; }

private:
    wxWebKitJSValue *m_jsresult;

    wxDECLARE_NO_COPY_CLASS(wxWebKitJavascriptResult);
};

#ifndef __WXGTK4__

// ----------------------------------------------------------------------------
// RAII wrapper of JSStringRef, also providing conversion to wxString
// ----------------------------------------------------------------------------

class wxJSStringRef
{
public:
    explicit wxJSStringRef(JSStringRef r) : m_jssref(r) { }
    ~wxJSStringRef() { JSStringRelease(m_jssref); }

    wxString ToWxString() const
    {
        const size_t length = JSStringGetMaximumUTF8CStringSize(m_jssref);

        wxCharBuffer str(length);

        JSStringGetUTF8CString(m_jssref, str.data(), length);

        return wxString::FromUTF8(str);
    }

private:
    JSStringRef m_jssref;

    wxDECLARE_NO_COPY_CLASS(wxJSStringRef);
};

#endif // !__WXGTK4__

#endif // _WX_GTK_PRIVATE_WEBKIT_H_
