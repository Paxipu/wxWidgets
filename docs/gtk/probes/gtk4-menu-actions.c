/*
 * gtk4-menu-actions.c -- probe the GTK4 menu-model mechanics wxWidgets needs.
 *
 * The GTK3 menu backend is built from widgets (GtkMenu, GtkMenuItem,
 * GtkCheckMenuItem, GtkRadioMenuItem, GtkMenuBar) and every one of them is
 * gone under GTK4.  The replacement is declarative: a GMenuModel describes
 * the structure and GAction objects carry the behaviour.  That is a different
 * shape, not a renamed API, so before rewriting src/gtk/menu.cpp we check the
 * specific mechanics the wx API depends on:
 *
 *   (1) can a GtkShortcutController + gtk_named_action_new() activate an
 *       action that lives in a group inserted with
 *       gtk_widget_insert_action_group()?  (menu accelerators)
 *   (2) does the "accel" menu-item attribute survive into the model?
 *       (accelerator *display* next to the item label)
 *   (3) do radio items -- one shared stateful string action plus per-item
 *       targets -- report the activated target?
 *   (4) does g_menu_item_set_attribute_value() let us stash a private
 *       attribute (we need a stable per-item handle)?
 *   (5) can a GtkPopoverMenu be built, parented and shown without a
 *       GtkApplicationWindow, and does it report open/close?
 *   (6) does a disabled action make its menu item insensitive?
 *
 * Build:
 *   gcc -o /tmp/probe gtk4-menu-actions.c $(pkg-config --cflags --libs gtk4)
 * Run under a display:
 *   xvfb-run -a /tmp/probe
 */

#include <gtk/gtk.h>
#include <string.h>

static int   g_normalActivations;
static char  g_radioTarget[64];
static int   g_checkState = -1;
static int   g_popoverShown;
static int   g_popoverClosed;

static void on_normal(GSimpleAction* a, GVariant* p, gpointer d)
{
    (void)a; (void)p; (void)d;
    g_normalActivations++;
}

static void on_radio_change(GSimpleAction* a, GVariant* value, gpointer d)
{
    (void)d;
    g_strlcpy(g_radioTarget, g_variant_get_string(value, NULL), sizeof(g_radioTarget));
    g_simple_action_set_state(a, value);
}

static void on_check_change(GSimpleAction* a, GVariant* value, gpointer d)
{
    (void)d;
    g_checkState = g_variant_get_boolean(value) ? 1 : 0;
    g_simple_action_set_state(a, value);
}

static void on_popover_show(GtkWidget* w, gpointer d)
{
    (void)w; (void)d;
    g_popoverShown++;
}

static void on_popover_closed(GtkPopover* p, gpointer d)
{
    (void)p; (void)d;
    g_popoverClosed++;
}

/* Pump the main loop a bounded number of times so the probe can never hang. */
static void pump(int iterations)
{
    int i;
    for ( i = 0; i < iterations; i++ )
        g_main_context_iteration(NULL, FALSE);
}

int main(void)
{
    GtkWidget* win;
    GtkWidget* box;
    GSimpleActionGroup* group;
    GSimpleAction* actNormal;
    GSimpleAction* actCheck;
    GSimpleAction* actRadio;
    GSimpleAction* actDisabled;
    GMenu* model;
    GMenuItem* item;
    GtkEventController* shortcuts;
    GtkShortcut* shortcut;
    GtkWidget* popover;
    GtkWidget* menubar;
    GMenu* barModel;
    GVariant* accel;
    GVariant* priv;
    gboolean ok;

    if ( !gtk_init_check() )
    {
        g_printerr("no display; skipping\n");
        return 77;
    }

    win = gtk_window_new();
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(win), box);

    /* ---- action group, inserted the way wxMenu will insert it ---------- */
    group = g_simple_action_group_new();

    actNormal = g_simple_action_new("i1", NULL);
    g_signal_connect(actNormal, "activate", G_CALLBACK(on_normal), NULL);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(actNormal));

    actCheck = g_simple_action_new_stateful("i2", NULL, g_variant_new_boolean(FALSE));
    g_signal_connect(actCheck, "change-state", G_CALLBACK(on_check_change), NULL);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(actCheck));

    actRadio = g_simple_action_new_stateful("r1", G_VARIANT_TYPE_STRING,
                                            g_variant_new_string("a"));
    g_signal_connect(actRadio, "change-state", G_CALLBACK(on_radio_change), NULL);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(actRadio));

    actDisabled = g_simple_action_new("i9", NULL);
    g_simple_action_set_enabled(actDisabled, FALSE);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(actDisabled));

    gtk_widget_insert_action_group(win, "wxm0", G_ACTION_GROUP(group));

    /* ---- (1) shortcut controller + named action ------------------------ */
    shortcuts = gtk_shortcut_controller_new();
    gtk_shortcut_controller_set_scope(GTK_SHORTCUT_CONTROLLER(shortcuts),
                                      GTK_SHORTCUT_SCOPE_GLOBAL);
    gtk_widget_add_controller(win, shortcuts);

    shortcut = gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_q, GDK_CONTROL_MASK),
                                gtk_named_action_new("wxm0.i1"));
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(shortcuts), shortcut);

    gtk_window_present(GTK_WINDOW(win));
    pump(200);

    /* Activating the shortcut directly is what a key press would end up
       doing; this checks that the named action resolves against the group we
       inserted on the widget, which is the part that could plausibly fail. */
    gtk_shortcut_action_activate(gtk_shortcut_get_action(shortcut),
                                 GTK_SHORTCUT_ACTION_EXCLUSIVE,
                                 win, NULL);
    pump(50);
    g_print("(1) named action via shortcut controller resolved: %s\n",
            g_normalActivations == 1 ? "yes" : "NO");

    /* ---- (2)+(4) attributes round-trip through the model ---------------- */
    model = g_menu_new();

    item = g_menu_item_new("_Quit", "wxm0.i1");
    g_menu_item_set_attribute(item, "accel", "s", "<Control>q");
    g_menu_item_set_attribute(item, "wx-id", "i", 5101);
    g_menu_append_item(model, item);
    g_object_unref(item);

    item = g_menu_item_new("_Check me", "wxm0.i2");
    g_menu_append_item(model, item);
    g_object_unref(item);

    /* radio group: one action, three targets */
    item = g_menu_item_new("Radio _A", NULL);
    g_menu_item_set_action_and_target_value(item, "wxm0.r1", g_variant_new_string("a"));
    g_menu_append_item(model, item);
    g_object_unref(item);
    item = g_menu_item_new("Radio _B", NULL);
    g_menu_item_set_action_and_target_value(item, "wxm0.r1", g_variant_new_string("b"));
    g_menu_append_item(model, item);
    g_object_unref(item);

    item = g_menu_item_new("Disabled", "wxm0.i9");
    g_menu_append_item(model, item);
    g_object_unref(item);

    accel = g_menu_model_get_item_attribute_value(G_MENU_MODEL(model), 0, "accel",
                                                  G_VARIANT_TYPE_STRING);
    g_print("(2) accel attribute survives into the model: %s (%s)\n",
            accel ? "yes" : "NO",
            accel ? g_variant_get_string(accel, NULL) : "-");
    if ( accel )
        g_variant_unref(accel);

    priv = g_menu_model_get_item_attribute_value(G_MENU_MODEL(model), 0, "wx-id",
                                                 G_VARIANT_TYPE_INT32);
    g_print("(4) private attribute survives into the model: %s (%d)\n",
            priv ? "yes" : "NO",
            priv ? g_variant_get_int32(priv) : -1);
    if ( priv )
        g_variant_unref(priv);

    /* ---- (3) radio activation reports the target ----------------------- */
    g_action_group_activate_action(G_ACTION_GROUP(group), "r1",
                                   g_variant_new_string("b"));
    pump(20);
    g_print("(3) radio activation reported target: %s (\"%s\")\n",
            strcmp(g_radioTarget, "b") == 0 ? "yes" : "NO", g_radioTarget);

    /* check item: activating a boolean stateful action with no parameter
       toggles it, which is what clicking the menu item does */
    g_action_group_activate_action(G_ACTION_GROUP(group), "i2", NULL);
    pump(20);
    g_print("    check activation toggled state: %s (%d)\n",
            g_checkState == 1 ? "yes" : "NO", g_checkState);

    /* ---- (6) disabled action ------------------------------------------- */
    ok = g_action_group_get_action_enabled(G_ACTION_GROUP(group), "i9");
    g_print("(6) disabled action reports disabled: %s\n", !ok ? "yes" : "NO");

    /* ---- (5) popover menu ---------------------------------------------- */
    popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(model));
    gtk_widget_set_parent(popover, box);
    g_signal_connect(popover, "show", G_CALLBACK(on_popover_show), NULL);
    g_signal_connect(popover, "closed", G_CALLBACK(on_popover_closed), NULL);
    gtk_popover_popup(GTK_POPOVER(popover));
    pump(200);
    gtk_popover_popdown(GTK_POPOVER(popover));
    pump(200);
    g_print("(5) popover menu shown/closed signals: %s (%d/%d)\n",
            (g_popoverShown && g_popoverClosed) ? "yes" : "NO",
            g_popoverShown, g_popoverClosed);

    /* changing the model after the popover was built */
    g_menu_append(model, "Added later", "wxm0.i1");
    pump(20);
    g_print("    model item count after late append: %d\n",
            g_menu_model_get_n_items(G_MENU_MODEL(model)));

    /* ---- menubar -------------------------------------------------------- */
    barModel = g_menu_new();
    g_menu_append_submenu(barModel, "_File", G_MENU_MODEL(model));
    menubar = gtk_popover_menu_bar_new_from_model(G_MENU_MODEL(barModel));
    gtk_box_append(GTK_BOX(box), menubar);
    pump(100);
    g_print("(7) popover menu bar built and realized: %s\n",
            gtk_widget_get_realized(menubar) ? "yes" : "NO");

    /* changing a submenu label requires remove+insert; check it works */
    g_menu_remove(barModel, 0);
    g_menu_append_submenu(barModel, "_Renamed", G_MENU_MODEL(model));
    pump(100);
    g_print("    menubar survives remove+insert of its submenu: %s\n",
            gtk_widget_get_realized(menubar) ? "yes" : "NO");

    gtk_widget_unparent(popover);
    return 0;
}
