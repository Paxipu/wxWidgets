///////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/dataform.h
// Purpose:     declaration of the wxDataFormat class
// Author:      Vadim Zeitlin
// Created:     19.10.99 (extracted from gtk/dataobj.h)
// Copyright:   (c) 1998 Vadim Zeitlin <zeitlin@dptmaths.ens-cachan.fr>
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GTK_DATAFORM_H
#define _WX_GTK_DATAFORM_H

class WXDLLIMPEXP_CORE wxDataFormat
{
public:
#ifdef __WXGTK4__
    // GdkAtom is gone under GTK4: a clipboard format is just a MIME type
    // string now. Formats are interned with g_intern_string(), which returns a
    // canonical pointer per string, so comparing them by pointer keeps working
    // exactly as it did for atoms -- an atom was an interned string too.
    typedef const char* NativeFormat;
#else
    // the clipboard formats under GDK are GdkAtoms
    typedef GdkAtom NativeFormat;
#endif

    wxDataFormat();
    wxDataFormat( wxDataFormatId type );
    wxDataFormat( NativeFormat format );

    // we have to provide all the overloads to allow using strings instead of
    // data formats (as a lot of existing code does)
    wxDataFormat( const wxString& id ) { InitFromString(id); }
#if !defined(wxNO_IMPLICIT_WXSTRING_ENCODING) && !defined(__WXGTK4__)
    // Under GTK4 this would be the same signature as the NativeFormat ctor
    // above, as a native format is a MIME type string there. The two are
    // interchangeable in that case: SetId(NativeFormat) interns whatever it is
    // given, so passing an ordinary literal works.
    wxDataFormat( const char *id ) { InitFromString(id); }
#endif
    wxDataFormat( const wchar_t *id ) { InitFromString(id); }
    wxDataFormat( const wxCStrData& id ) { InitFromString(id); }

    wxDataFormat& operator=(NativeFormat format)
        { SetId(format); return *this; }

    // comparison
    bool operator==(wxDataFormatId type) const
        { return m_type == type; }
    bool operator!=(wxDataFormatId type) const
        { return !(*this == type); }
    bool operator==(NativeFormat format) const
        { return m_format == (NativeFormat)format; }
    bool operator!=(NativeFormat format) const
        { return !(*this == (NativeFormat)format); }
    bool operator==(const wxDataFormat& other) const;
    bool operator!=(const wxDataFormat& other) const
        { return !(*this == other); }

    // explicit and implicit conversions to NativeFormat which is one of
    // standard data types (implicit conversion is useful for preserving the
    // compatibility with old code)
    NativeFormat GetFormatId() const { return m_format; }
    operator NativeFormat() const { return m_format; }

    void SetId( NativeFormat format );

    // string ids are used for custom types - this SetId() must be used for
    // application-specific formats
    wxString GetId() const;
    void SetId( const wxString& id );

    // implementation
    wxDataFormatId GetType() const;
    void SetType( wxDataFormatId type );

private:
    // common part of ctors from format name
    void InitFromString(const wxString& id);

    wxDataFormatId   m_type;
    NativeFormat     m_format;
};

#endif // _WX_GTK_DATAFORM_H
