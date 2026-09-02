/////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/colour.h
// Purpose:
// Author:      Robert Roebling
// Copyright:   (c) 1998 Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GTK_COLOUR_H_
#define _WX_GTK_COLOUR_H_

#ifdef __WXGTK3__
typedef struct _GdkRGBA GdkRGBA;
#endif

//-----------------------------------------------------------------------------
// wxColour
//-----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxColourImpl : public wxColourBase
{
public:
    wxColourImpl() = default;

    // port-specific constructors
    // ------------
#ifndef __WXGTK4__
    // GdkColor is gone under GTK4; GdkRGBA is the only representation left.
    wxColourImpl(const GdkColor& gdkColor);
#endif
#ifdef __WXGTK3__
    wxColourImpl(const GdkRGBA& gdkRGBA);
#endif

    bool operator==(const wxColourImpl& col) const;
    bool operator!=(const wxColourImpl& col) const { return !(*this == col); }

    unsigned char Red() const override;
    unsigned char Green() const override;
    unsigned char Blue() const override;
    unsigned char Alpha() const override;

    // Implementation part
#ifdef __WXGTK3__
    const GdkRGBA* GTKGetRGBA() const;
#else
    void CalcPixel( GdkColormap *cmap );
    int GetPixel() const;
#endif
#ifndef __WXGTK4__
    const GdkColor *GetColor() const;
#endif

protected:
    virtual void
    InitRGBA(unsigned char r, unsigned char g, unsigned char b, unsigned char a) override;
};

#endif // _WX_GTK_COLOUR_H_
