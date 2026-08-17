///////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/private/image.h
// Author:      Paul Cornett
// Copyright:   (c) 2020 Paul Cornett
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

// Class that can be used in place of GtkImage, to allow drawing of alternate
// bitmaps, such as HiDPI or disabled.

#ifdef __WXGTK4__

// GtkImage is a final type under GTK4, so it cannot be derived from and the
// GTK3 class below has no GTK4 form. What that class exists to do -- pick the
// right bitmap variant for the widget's scale factor and enabled state at the
// moment it draws -- is therefore done eagerly instead: the chosen bitmap is
// pushed into a plain GtkImage as a GdkTexture, and callers re-push it when
// the state it depends on changes.
//
// The interface is kept deliberately close to the GTK3 one so that call sites
// differ by as little as possible; only "image->Set(b)" becomes
// "wxGtkImage::Set(image, b)".
class wxGtkImage
{
public:
    // Create a plain GtkImage for wx to keep up to date. The window, if given,
    // is only used to pick the bitmap variant matching its scale factor.
    static GtkWidget* New(wxWindow* win = nullptr);

    // Whether this widget is one of ours, i.e. a GtkImage.
    static bool Is(GtkWidget* widget);

    // Show this bundle, choosing the variant for the scale factor of the image
    // widget itself, which is more accurate than the window passed to New().
    static void Set(GtkWidget* image, const wxBitmapBundle& bitmapBundle);

    // As above, but showing the disabled appearance: if the bundle has no
    // disabled variant one is derived from the normal one.
    static void SetDisabled(GtkWidget* image,
                            const wxBitmapBundle& normal,
                            const wxBitmapBundle& disabled);
};

#define WX_GTK_IMAGE(obj) (GTK_WIDGET(obj))
#define WX_GTK_IS_IMAGE(obj) wxGtkImage::Is(GTK_WIDGET(obj))

#else // !__WXGTK4__

class wxGtkImage: GtkImage
{
public:
    struct BitmapProvider
    {
        virtual ~BitmapProvider() = default;

        virtual wxBitmap Get(int scale) const = 0;
        virtual void Set(const wxBitmapBundle&) { }

        // Simple helpers used in implementation.
        static wxBitmap GetAtScale(const wxBitmapBundle& b, int scale)
        {
            return b.GetBitmap(b.GetDefaultSize() * scale);
        }
    };

    static GType Type();
    static GtkWidget* New(BitmapProvider* provider);
    static GtkWidget* New(wxWindow* win = nullptr);

    // Use bitmaps from the given bundle, the logical bitmap size is the
    // default size of the bundle.
    void Set(const wxBitmapBundle& bitmapBundle);

    // This pointer is never null and is owned by this class.
    BitmapProvider* m_provider;

    wxDECLARE_NO_COPY_CLASS(wxGtkImage);

    // This class is constructed by New() and destroyed by its GObject
    // finalizer, so neither its ctor nor dtor can ever be used.
    wxGtkImage() wxMEMBER_DELETE;
    ~wxGtkImage() wxMEMBER_DELETE;
};

#define WX_GTK_IMAGE(obj) G_TYPE_CHECK_INSTANCE_CAST(obj, wxGtkImage::Type(), wxGtkImage)
#define WX_GTK_IS_IMAGE(obj) G_TYPE_CHECK_INSTANCE_TYPE(obj, wxGtkImage::Type())

#endif // __WXGTK4__/!__WXGTK4__
