/*
 * Probe: can GTK4 expose accessible children that are not widgets?
 *
 * wxWidgets' accessibility API is modelled on MSAA: a wxAccessible answers
 * questions about "child ids", integers naming things that have no window of
 * their own -- a wxGrid cell, a wxDataViewCtrl item, a range of wxRichTextCtrl.
 * GTK+ 3 could express that through ATK, where an AtkObject was just an object.
 * GTK4 dropped ATK, and its replacement talks about widgets almost everywhere.
 *
 * This asks whether the replacement can still do it, using three things GTK4
 * added in 4.10:
 *
 *   - GtkAccessible is an interface any GObject may implement, not a class
 *     only widgets get;
 *   - gtk_at_context_create() is public, so a non-widget can own the context
 *     that carries its role and properties;
 *   - GtkAccessibleInterface has get_first_accessible_child() and
 *     get_next_accessible_sibling(), so a widget can name children the widget
 *     tree does not contain.
 *
 * It also reports two things that are properties of the GTK in use rather than
 * of the design, and so are printed rather than asserted: which of the two
 * tree-walking vfuncs GTK actually calls, and an unbalanced unref in
 * gtk_accessible_update_next_accessible_sibling(). Both are explained in
 * docs/gtk/gtk4-accessibility.md.
 *
 * Build and run (no accessibility bus needed -- GTK_A11Y=test keeps everything
 * in process, which is also what makes this shape testable in CI):
 *
 *   cc -o gtk4-a11y-virtual-child gtk4-a11y-virtual-child.c $(pkg-config --cflags --libs gtk4)
 *   GTK_A11Y=test ./gtk4-a11y-virtual-child
 */

#include <gtk/gtk.h>

/* Set by the two tree-walking vfuncs, to see which of them GTK actually uses. */
static gboolean g_vfunc_called;

/* ------------------------------------------------------------------------ */
/* A virtual child: a plain GObject that claims to be a grid cell.          */
/* ------------------------------------------------------------------------ */

#define PROBE_TYPE_CELL (probe_cell_get_type())
G_DECLARE_FINAL_TYPE(ProbeCell, probe_cell, PROBE, CELL, GObject)

struct _ProbeCell
{
    GObject parent_instance;

    GtkATContext* context;
    GtkAccessible* parent;
    GtkAccessible* next;
    char* label;
};

static void probe_cell_accessible_init(GtkAccessibleInterface* iface);

G_DEFINE_TYPE_WITH_CODE(ProbeCell, probe_cell, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(GTK_TYPE_ACCESSIBLE,
                                              probe_cell_accessible_init))

static GtkATContext* probe_cell_get_at_context(GtkAccessible* accessible)
{
    ProbeCell* cell = PROBE_CELL(accessible);

    if (cell->context == NULL)
    {
        cell->context = gtk_at_context_create(GTK_ACCESSIBLE_ROLE_GRID_CELL,
                                              accessible,
                                              gdk_display_get_default());
    }

    return cell->context ? g_object_ref(cell->context) : NULL;
}

static gboolean probe_cell_get_platform_state(GtkAccessible* accessible,
                                              GtkAccessiblePlatformState state)
{
    (void)accessible;

    /* A cell can be focused within its grid, but never holds the toolkit
       focus itself: the grid widget does. */
    return state == GTK_ACCESSIBLE_PLATFORM_STATE_FOCUSABLE;
}

static GtkAccessible* probe_cell_get_accessible_parent(GtkAccessible* accessible)
{
    ProbeCell* cell = PROBE_CELL(accessible);

    return cell->parent ? g_object_ref(cell->parent) : NULL;
}

static GtkAccessible* probe_cell_get_first_accessible_child(GtkAccessible* accessible)
{
    (void)accessible;
    return NULL;
}

static GtkAccessible* probe_cell_get_next_accessible_sibling(GtkAccessible* accessible)
{
    ProbeCell* cell = PROBE_CELL(accessible);

    g_vfunc_called = TRUE;

    return cell->next ? g_object_ref(cell->next) : NULL;
}

static gboolean probe_cell_get_bounds(GtkAccessible* accessible,
                                      int* x, int* y, int* width, int* height)
{
    (void)accessible;

    *x = 0;
    *y = 0;
    *width = 40;
    *height = 20;

    return TRUE;
}

static void probe_cell_accessible_init(GtkAccessibleInterface* iface)
{
    iface->get_at_context = probe_cell_get_at_context;
    iface->get_platform_state = probe_cell_get_platform_state;
    iface->get_accessible_parent = probe_cell_get_accessible_parent;
    iface->get_first_accessible_child = probe_cell_get_first_accessible_child;
    iface->get_next_accessible_sibling = probe_cell_get_next_accessible_sibling;
    iface->get_bounds = probe_cell_get_bounds;
}

static void probe_cell_finalize(GObject* object)
{
    ProbeCell* cell = PROBE_CELL(object);

    g_clear_object(&cell->context);
    g_free(cell->label);

    G_OBJECT_CLASS(probe_cell_parent_class)->finalize(object);
}

/* GtkAccessible carries an "accessible-role" property, and GObject requires
   every implementer of an interface to install its properties: without this
   the cell is constructed with a warning and its role never resolves. */
enum
{
    PROP_CELL_ACCESSIBLE_ROLE = 1
};

static void probe_cell_get_property(GObject* object, guint prop_id,
                                    GValue* value, GParamSpec* pspec)
{
    switch (prop_id)
    {
        case PROP_CELL_ACCESSIBLE_ROLE:
            g_value_set_enum(value, GTK_ACCESSIBLE_ROLE_GRID_CELL);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

static void probe_cell_set_property(GObject* object, guint prop_id,
                                    const GValue* value, GParamSpec* pspec)
{
    (void)value;

    switch (prop_id)
    {
        case PROP_CELL_ACCESSIBLE_ROLE:
            /* Fixed for this class. */
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

static void probe_cell_class_init(ProbeCellClass* klass)
{
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = probe_cell_finalize;
    gobject_class->get_property = probe_cell_get_property;
    gobject_class->set_property = probe_cell_set_property;

    g_object_class_override_property(gobject_class, PROP_CELL_ACCESSIBLE_ROLE,
                                     "accessible-role");
}

static void probe_cell_init(ProbeCell* cell)
{
    (void)cell;
}

/* ------------------------------------------------------------------------ */
/* A widget standing in for wxPizza: it owns cells the widget tree lacks.   */
/* ------------------------------------------------------------------------ */

#define PROBE_TYPE_GRID (probe_grid_get_type())
G_DECLARE_FINAL_TYPE(ProbeGrid, probe_grid, PROBE, GRID, GtkWidget)

struct _ProbeGrid
{
    GtkWidget parent_instance;

    ProbeCell* cells[2];
};

static void probe_grid_accessible_init(GtkAccessibleInterface* iface);

G_DEFINE_TYPE_WITH_CODE(ProbeGrid, probe_grid, GTK_TYPE_WIDGET,
                        G_IMPLEMENT_INTERFACE(GTK_TYPE_ACCESSIBLE,
                                              probe_grid_accessible_init))

/* Re-implementing the interface hides GtkWidget's implementation, so keep
   hold of it: everything except the child walk still belongs to the widget. */
static GtkAccessibleInterface* probe_grid_parent_iface = NULL;

static GtkAccessible* probe_grid_get_first_accessible_child(GtkAccessible* accessible)
{
    ProbeGrid* grid = PROBE_GRID(accessible);

    g_vfunc_called = TRUE;

    return GTK_ACCESSIBLE(g_object_ref(grid->cells[0]));
}

static GtkATContext* probe_grid_get_at_context(GtkAccessible* accessible)
{
    return probe_grid_parent_iface->get_at_context(accessible);
}

static gboolean probe_grid_get_platform_state(GtkAccessible* accessible,
                                              GtkAccessiblePlatformState state)
{
    return probe_grid_parent_iface->get_platform_state(accessible, state);
}

static GtkAccessible* probe_grid_get_accessible_parent(GtkAccessible* accessible)
{
    return probe_grid_parent_iface->get_accessible_parent(accessible);
}

static GtkAccessible* probe_grid_get_next_accessible_sibling(GtkAccessible* accessible)
{
    return probe_grid_parent_iface->get_next_accessible_sibling(accessible);
}

static gboolean probe_grid_get_bounds(GtkAccessible* accessible,
                                      int* x, int* y, int* width, int* height)
{
    return probe_grid_parent_iface->get_bounds(accessible, x, y, width, height);
}

static void probe_grid_accessible_init(GtkAccessibleInterface* iface)
{
    probe_grid_parent_iface = g_type_interface_peek_parent(iface);

    iface->get_at_context = probe_grid_get_at_context;
    iface->get_platform_state = probe_grid_get_platform_state;
    iface->get_accessible_parent = probe_grid_get_accessible_parent;
    iface->get_first_accessible_child = probe_grid_get_first_accessible_child;
    iface->get_next_accessible_sibling = probe_grid_get_next_accessible_sibling;
    iface->get_bounds = probe_grid_get_bounds;
}

static void probe_grid_dispose(GObject* object)
{
    ProbeGrid* grid = PROBE_GRID(object);

    for (unsigned n = 0; n < G_N_ELEMENTS(grid->cells); n++)
        g_clear_object(&grid->cells[n]);

    G_OBJECT_CLASS(probe_grid_parent_class)->dispose(object);
}

static void probe_grid_class_init(ProbeGridClass* klass)
{
    G_OBJECT_CLASS(klass)->dispose = probe_grid_dispose;

    gtk_widget_class_set_accessible_role(GTK_WIDGET_CLASS(klass),
                                         GTK_ACCESSIBLE_ROLE_GRID);
}

static void probe_grid_init(ProbeGrid* grid)
{
    for (unsigned n = 0; n < G_N_ELEMENTS(grid->cells); n++)
    {
        grid->cells[n] = g_object_new(PROBE_TYPE_CELL, NULL);
        grid->cells[n]->label = g_strdup_printf("cell %u", n);
    }
}

/*
 * Both the parent and the next sibling are given in one call.  Doing it in two
 * -- set_accessible_parent() and then update_next_accessible_sibling() -- looks
 * equivalent and is not: see the reference-count check at the end of main().
 */
static void probe_grid_connect_cells(ProbeGrid* grid)
{
    gtk_accessible_set_accessible_parent(GTK_ACCESSIBLE(grid->cells[0]),
                                         GTK_ACCESSIBLE(grid),
                                         GTK_ACCESSIBLE(grid->cells[1]));

    gtk_accessible_set_accessible_parent(GTK_ACCESSIBLE(grid->cells[1]),
                                         GTK_ACCESSIBLE(grid),
                                         NULL);
}

/* ------------------------------------------------------------------------ */

static int failures = 0;

static void check(gboolean ok, const char* what)
{
    g_print("%-58s %s\n", what, ok ? "yes" : "NO");
    if (!ok)
        failures++;
}

int main(void)
{
    gtk_init();

    GtkWidget* window = gtk_window_new();
    GtkWidget* grid = g_object_new(PROBE_TYPE_GRID, NULL);
    ProbeGrid* pg = PROBE_GRID(grid);

    gtk_window_set_child(GTK_WINDOW(window), grid);
    probe_grid_connect_cells(pg);

    /* 1. Does a non-widget GObject get a working accessible role? */
    check(gtk_test_accessible_has_role(GTK_ACCESSIBLE(pg->cells[0]),
                                       GTK_ACCESSIBLE_ROLE_GRID_CELL),
          "non-widget GObject carries an accessible role");

    /* 2. Can properties be pushed to it and read back? */
    gtk_accessible_update_property(GTK_ACCESSIBLE(pg->cells[0]),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   pg->cells[0]->label,
                                   -1);

    char* mismatch = gtk_test_accessible_check_property(GTK_ACCESSIBLE(pg->cells[0]),
                                                        GTK_ACCESSIBLE_PROPERTY_LABEL,
                                                        "cell 0");
    check(mismatch == NULL, "properties pushed to it are readable");
    if (mismatch)
        g_print("    got: %s\n", mismatch);
    g_free(mismatch);

    /* 3. Can states be pushed to it? */
    gtk_accessible_update_state(GTK_ACCESSIBLE(pg->cells[0]),
                                GTK_ACCESSIBLE_STATE_SELECTED, TRUE,
                                -1);
    mismatch = gtk_test_accessible_check_state(GTK_ACCESSIBLE(pg->cells[0]),
                                               GTK_ACCESSIBLE_STATE_SELECTED,
                                               TRUE);
    check(mismatch == NULL, "states pushed to it are readable");
    g_free(mismatch);

    /* 4. Is it reachable from the widget, i.e. really in the tree? */
    GtkAccessible* first = gtk_accessible_get_first_accessible_child(GTK_ACCESSIBLE(grid));
    check(first == GTK_ACCESSIBLE(pg->cells[0]),
          "widget can name a non-widget as its first child");
    g_clear_object(&first);

    /* 5. And are its siblings walkable? */
    GtkAccessible* next = gtk_accessible_get_next_accessible_sibling(GTK_ACCESSIBLE(pg->cells[0]));
    check(next == GTK_ACCESSIBLE(pg->cells[1]),
          "the child chain continues to the next sibling");
    g_clear_object(&next);

    /* 5b. But is that the vfunc's answer, or the stored one? The two are
          declared side by side on GtkAccessibleInterface and only one of them
          is consulted, which decides whether the objects can be made as the
          walk reaches them or have to exist before it starts. */
    g_vfunc_called = FALSE;
    next = gtk_accessible_get_first_accessible_child(GTK_ACCESSIBLE(grid));
    g_clear_object(&next);
    check(g_vfunc_called,
          "get_first_accessible_child() calls the interface vfunc");

    g_vfunc_called = FALSE;
    next = gtk_accessible_get_next_accessible_sibling(GTK_ACCESSIBLE(pg->cells[0]));
    g_clear_object(&next);
    g_print("%-58s %s\n",
            "get_next_accessible_sibling() calls the interface vfunc",
            g_vfunc_called ? "yes" : "no  <-- it returns the stored value");

    /* 6. Does the child point back at the widget? */
    GtkAccessible* parent = gtk_accessible_get_accessible_parent(GTK_ACCESSIBLE(pg->cells[0]));
    check(parent == GTK_ACCESSIBLE(grid),
          "the child points back at the widget");
    g_clear_object(&parent);

    /* 7. Can a widget's role be overridden per instance, or only per class?
          wxWidgets needs this: one wxPizza subclass backs every custom-drawn
          control, so the role cannot live on the GtkWidgetClass. */
    GtkWidget* other = g_object_new(PROBE_TYPE_GRID,
                                    "accessible-role", GTK_ACCESSIBLE_ROLE_TREE_GRID,
                                    NULL);
    g_object_ref_sink(other);

    check(gtk_test_accessible_has_role(GTK_ACCESSIBLE(other),
                                       GTK_ACCESSIBLE_ROLE_TREE_GRID),
          "a widget's role can be overridden per instance");
    g_object_unref(other);

    /* 8. Not a capability question but a trap worth recording: reaching the
          same state in two calls instead of one drops a reference on the
          parent widget, which is enough to finalize it.  Reported separately
          because it is a property of the GTK in use, not of the design. */
    GtkWidget* holder = gtk_window_new();
    GtkWidget* victim = gtk_drawing_area_new();
    GtkWidget* sibling = g_object_ref_sink(gtk_drawing_area_new());
    GObject* lone = g_object_ref_sink(g_object_new(GTK_TYPE_LABEL, NULL));

    gtk_window_set_child(GTK_WINDOW(holder), victim);
    g_object_ref(victim);

    const unsigned before = ((GObject*)victim)->ref_count;
    gtk_accessible_set_accessible_parent(GTK_ACCESSIBLE(lone),
                                         GTK_ACCESSIBLE(victim), NULL);
    gtk_accessible_update_next_accessible_sibling(GTK_ACCESSIBLE(lone),
                                                  GTK_ACCESSIBLE(sibling));
    const unsigned after = ((GObject*)victim)->ref_count;

    g_print("\ngtk_accessible_update_next_accessible_sibling() leaves the\n"
            "accessible parent's reference count at %u, was %u%s\n",
            after, before,
            after == before ? "" : "  <-- unbalanced unref, see the notes above");

    g_object_unref(lone);
    g_object_unref(sibling);
    gtk_window_destroy(GTK_WINDOW(holder));

    gtk_window_destroy(GTK_WINDOW(window));

    g_print("\n%d check(s) failed\n", failures);

    return failures != 0;
}
