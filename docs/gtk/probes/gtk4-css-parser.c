// Probe: two ways GTK4's CSS parser is stricter than GTK3's.
//
// Running test_gui under GTK4 produced about four thousand
//
//     Theme parser warning: <data>:1:NN-NN: Expected ';' at end of block
//
// warnings, all of them from the CSS wx builds in
// wxWindowGTK::GTKApplyWidgetStyle() and its neighbours. The message is about
// the *last* declaration in a block, and it is not obvious from reading it
// that that is what it means, so this isolates the two rules by comparing
// otherwise identical stylesheets.
//
// 1. A declaration block's final declaration must be terminated with a
//    semicolon. GTK3 accepted "*{color:red}"; GTK4 does not. Everything wx
//    generated was written in the shorter form.
//
// 2. The "font" shorthand needs a unit on the size. A Pango font description
//    stringifies as e.g. "Sans 10", which is not valid CSS -- the size has to
//    be "10pt". This is the source of the "Expected a number" warnings.
//
// Neither is called out in the GTK4 migration guide, which is why these are
// also checked by build/tools/gtk4-invariants.c.
//
// Build with:
//   gcc -o gtk4-css-parser gtk4-css-parser.c $(pkg-config --cflags --libs gtk4)

#include <gtk/gtk.h>

static int g_errs;

static void on_error(GtkCssProvider*, GtkCssSection*, const GError* e, gpointer)
{
    g_errs++;
    g_print("      -> %s\n", e->message);
}

static void try_css(const char* label, const char* css)
{
    GtkCssProvider* const p = gtk_css_provider_new();

    g_errs = 0;
    g_signal_connect(p, "parsing-error", G_CALLBACK(on_error), NULL);
    gtk_css_provider_load_from_data(p, css, -1);

    g_print("%-6s %-46s %s\n", g_errs ? "FAIL" : "ok", css, label);

    g_object_unref(p);
}

int main(int argc, char** argv)
{
    gtk_init();

    g_print("Trailing semicolon on the last declaration:\n");
    try_css("GTK3 accepted this", "*{color:rgb(0,0,0)}");
    try_css("GTK4 wants this",    "*{color:rgb(0,0,0);}");

    // Not specific to a simple selector: the same holds for any block.
    try_css("GTK3 accepted this", "* undershoot{background:transparent}");
    try_css("GTK4 wants this",    "* undershoot{background:transparent;}");

    g_print("\nUnit on the font shorthand's size:\n");
    try_css("as Pango prints it", "*{font:Sans 10;}");
    try_css("as CSS wants it",    "*{font:10pt Sans;}");

    return 0;
}
