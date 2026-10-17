/*
 * unity-gtk4-menu - export a GTK4 header bar menu to the Unity global menu.
 *
 * GTK4 applications keep their menu in a GtkMenuButton in the header bar
 * rather than in a menu bar, so Unity's panel has nothing to show for them.
 * GTK4 also removed the module loading mechanism appmenu-gtk-module uses to
 * reach GTK3 applications, so there is no module to write. This is an
 * LD_PRELOAD library instead: it overwrites realize in the GtkWindow class
 * vtable, finds the header bar's menu model through public API, and sets it as
 * the application menu bar before the window is realized. GTK4 exports it over
 * org.gtk.Menus by itself from there.
 *
 * NOTHING HERE MAY BE LINKED AGAINST GTK OR GLIB.
 *
 * A preloaded library enters every process on the machine, not only the
 * applications it was written for. An earlier version linked libgtk-4.so.1 and
 * called g_type_class_ref() from its constructor; installed session-wide it
 * dragged GTK4 into GTK3 processes and killed unity-settings-daemon, onboard,
 * apport-gtk and the indicators outright:
 *
 *     Gdk-ERROR: gdk_display_manager_get() was called before gtk_init()
 *
 * So every symbol is resolved with dlsym at runtime and the library does
 * nothing until GTK4 is loaded in this process by someone else - at startup
 * for a C application, through GObject Introspection for gjs and Python (see
 * g_module_symbol() below). The headers are
 * included for their type and struct definitions only - a header costs
 * nothing, a call costs a NEEDED entry. Never call a GTK or GLib function
 * directly here, and never use the GTK_IS_* or GTK_TYPE_* macros: they expand
 * into calls. Use is_a() and the cached types instead.
 *
 * gtk-nocsd, which ships in Ubuntu Unity and does the same job for window
 * decorations, is built this way for the same reason.
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <gio/gdesktopappinfo.h>
#include <gmodule.h>

#define SHIM_SCHEMA "com.ubuntu-unity.gtk4-menu"

static int debug_on;

static void note(const char *fmt, ...)
{
	if (!debug_on)
		return;

	va_list ap;
	const char *path = getenv("UNITY_GTK4_MENU_LOG");
	FILE *out = stderr;

	if (path != NULL) {
		FILE *f = fopen(path, "a");
		if (f != NULL)
			out = f;
	}

	fprintf(out, "[unity-gtk4-menu %d] ", (int)getpid());
	va_start(ap, fmt);
	vfprintf(out, fmt, ap);
	va_end(ap);
	fputc('\n', out);
	fflush(out);

	if (out != stderr)
		fclose(out);
}

/* ---- resolved symbols ---------------------------------------------------- */

#define DECL(ret, name, args) static ret (*p_##name) args

DECL(gpointer, g_type_class_ref, (GType));
DECL(gboolean, g_type_check_instance_is_a, (GTypeInstance *, GType));
DECL(void, g_object_unref, (gpointer));
DECL(GType, gtk_window_get_type, (void));
DECL(GType, gtk_application_window_get_type, (void));
DECL(GType, gtk_menu_button_get_type, (void));
DECL(GType, gtk_popover_menu_get_type, (void));
DECL(GType, gtk_popover_menu_bar_get_type, (void));
DECL(GtkWidget *, gtk_window_get_titlebar, (GtkWindow *));
DECL(GtkWidget *, gtk_window_get_child, (GtkWindow *));
DECL(GtkWidget *, gtk_widget_get_first_child, (GtkWidget *));
DECL(GtkWidget *, gtk_widget_get_next_sibling, (GtkWidget *));
DECL(GMenuModel *, gtk_menu_button_get_menu_model, (GtkMenuButton *));
DECL(GtkPopover *, gtk_menu_button_get_popover, (GtkMenuButton *));
DECL(GMenuModel *, gtk_popover_menu_get_menu_model, (GtkPopoverMenu *));
DECL(GMenuModel *, gtk_popover_menu_bar_get_menu_model, (GtkPopoverMenuBar *));
DECL(GtkApplication *, gtk_window_get_application, (GtkWindow *));
DECL(GMenuModel *, gtk_application_get_menubar, (GtkApplication *));
DECL(void, gtk_application_set_menubar, (GtkApplication *, GMenuModel *));
DECL(GMenu *, g_menu_new, (void));
DECL(gint, g_menu_model_get_n_items, (GMenuModel *));
DECL(GMenuModel *, g_menu_model_get_item_link, (GMenuModel *, gint, const gchar *));
DECL(GMenuItem *, g_menu_item_new_from_model, (GMenuModel *, gint));
DECL(void, g_menu_item_set_link, (GMenuItem *, const gchar *, GMenuModel *));
DECL(void, g_menu_append_item, (GMenu *, GMenuItem *));
DECL(GVariant *, g_menu_model_get_item_attribute_value,
     (GMenuModel *, gint, const gchar *, const GVariantType *));
DECL(void, g_variant_unref, (GVariant *));
DECL(void, g_menu_append_submenu, (GMenu *, const gchar *, GMenuModel *));
DECL(const gchar *, g_get_application_name, (void));
DECL(gchar *, g_strdup, (const gchar *));
DECL(gchar *, g_strconcat, (const gchar *, ...));
DECL(void, g_free, (gpointer));
DECL(const gchar *, g_application_get_application_id, (GApplication *));
DECL(GDesktopAppInfo *, g_desktop_app_info_new, (const gchar *));
DECL(const char *, g_app_info_get_name, (GAppInfo *));
DECL(GSettingsSchemaSource *, g_settings_schema_source_get_default, (void));
DECL(GSettingsSchema *, g_settings_schema_source_lookup,
     (GSettingsSchemaSource *, const gchar *, gboolean));
DECL(void, g_settings_schema_unref, (GSettingsSchema *));
DECL(GSettings *, g_settings_new, (const gchar *));
DECL(gboolean, g_settings_get_boolean, (GSettings *, const gchar *));
DECL(GtkWidget *, gtk_widget_get_parent, (GtkWidget *));
DECL(gboolean, gtk_menu_button_get_primary, (GtkMenuButton *));
DECL(gboolean, gtk_widget_is_visible, (GtkWidget *));
DECL(gboolean, gtk_widget_get_child_visible, (GtkWidget *));
DECL(GSimpleAction *, g_simple_action_new_stateful,
     (const gchar *, const GVariantType *, GVariant *));
DECL(void, g_simple_action_set_state, (GSimpleAction *, GVariant *));
DECL(GVariant *, g_action_get_state, (GAction *));
DECL(gulong, g_signal_connect_object,
     (gpointer, const gchar *, GCallback, gpointer, GConnectFlags));
DECL(GParamSpec *, g_object_class_find_property, (GObjectClass *, const gchar *));
DECL(void, g_object_get_property, (GObject *, const gchar *, GValue *));
DECL(GValue *, g_value_init, (GValue *, GType));
DECL(void, g_value_unset, (GValue *));
DECL(gboolean, g_value_get_boolean, (const GValue *));
DECL(gint, g_value_get_int, (const GValue *));
DECL(guint, g_value_get_uint, (const GValue *));
DECL(gdouble, g_value_get_double, (const GValue *));
DECL(gfloat, g_value_get_float, (const GValue *));
DECL(const gchar *, g_value_get_string, (const GValue *));
DECL(gint, g_value_get_enum, (const GValue *));
DECL(GEnumValue *, g_enum_get_value, (GEnumClass *, gint));
DECL(void, g_type_class_unref, (gpointer));
DECL(GType, g_type_fundamental, (GType));
DECL(GVariant *, g_variant_new_boolean, (gboolean));
DECL(GVariant *, g_variant_new_int32, (gint32));
DECL(GVariant *, g_variant_new_uint32, (guint32));
DECL(GVariant *, g_variant_new_double, (gdouble));
DECL(gboolean, g_variant_get_boolean, (GVariant *));
DECL(gboolean, g_variant_is_of_type, (GVariant *, const GVariantType *));
DECL(void, g_object_set_data, (GObject *, const gchar *, gpointer));
DECL(gpointer, g_object_get_data, (GObject *, const gchar *));
DECL(void, g_simple_action_set_enabled, (GSimpleAction *, gboolean));
DECL(gboolean, gtk_widget_class_query_action,
     (GtkWidgetClass *, guint, GType *, const char **, const GVariantType **,
      const char **));
DECL(gboolean, gtk_widget_activate_action_variant,
     (GtkWidget *, const char *, GVariant *));
DECL(GSimpleAction *, g_simple_action_new, (const gchar *, const GVariantType *));
DECL(void, g_action_map_add_action, (GActionMap *, GAction *));
DECL(GAction *, g_action_map_lookup_action, (GActionMap *, const gchar *));
DECL(gulong, g_signal_connect_data,
     (gpointer, const gchar *, GCallback, gpointer, GClosureNotify,
      GConnectFlags));
DECL(void, g_object_add_weak_pointer, (GObject *, gpointer *));
DECL(void, g_object_remove_weak_pointer, (GObject *, gpointer *));
DECL(const gchar *, g_variant_get_string, (GVariant *, gsize *));
DECL(GVariant *, g_variant_new_string, (const gchar *));
DECL(void, g_menu_item_set_attribute_value,
     (GMenuItem *, const gchar *, GVariant *));

/* menus that appear after realize (0.12) */
DECL(gpointer, g_object_ref, (gpointer));
DECL(void, g_object_set_data_full,
     (GObject *, const gchar *, gpointer, GDestroyNotify));
DECL(void, g_object_weak_ref, (GObject *, GWeakNotify, gpointer));
DECL(void, g_object_weak_unref, (GObject *, GWeakNotify, gpointer));
DECL(void, g_signal_handler_disconnect, (gpointer, gulong));
DECL(guint, g_idle_add, (GSourceFunc, gpointer));
DECL(gboolean, g_source_remove, (guint));
DECL(GType, g_type_from_name, (const gchar *));
DECL(GType *, g_type_children, (GType, guint *));
DECL(gpointer, g_type_class_peek, (GType));
DECL(gboolean, g_application_get_is_registered, (GApplication *));
DECL(gboolean, g_application_get_is_remote, (GApplication *));
DECL(GtkRoot *, gtk_widget_get_root, (GtkWidget *));
DECL(GType, gtk_header_bar_get_type, (void));
DECL(GType, gtk_popover_get_type, (void));
DECL(void, g_menu_remove, (GMenu *, gint));
DECL(void, g_menu_insert_item, (GMenu *, gint, GMenuItem *));
DECL(GMenuAttributeIter *, g_menu_model_iterate_item_attributes,
     (GMenuModel *, gint));
DECL(gboolean, g_menu_attribute_iter_get_next,
     (GMenuAttributeIter *, const gchar **, GVariant **));
DECL(GMenuLinkIter *, g_menu_model_iterate_item_links, (GMenuModel *, gint));
DECL(gboolean, g_menu_link_iter_get_next,
     (GMenuLinkIter *, const gchar **, GMenuModel **));
DECL(gboolean, g_variant_equal, (gconstpointer, gconstpointer));
DECL(GList *, gtk_application_get_windows, (GtkApplication *));

static GType type_window, type_app_window, type_menu_button;
static GType type_popover_menu, type_popover_menu_bar;
static GType type_header_bar, type_popover;

#define RESOLVE(handle, name)                                                  \
	do {                                                                   \
		*(void **)(&p_##name) = dlsym(handle, #name);                  \
		if (p_##name == NULL) {                                        \
			note("dlsym failed for %s", #name);                    \
			return 0;                                              \
		}                                                              \
	} while (0)

static int resolve_all(void *gtk, void *gobj, void *glib, void *gio)
{
	RESOLVE(gobj, g_type_class_ref);
	RESOLVE(gobj, g_type_check_instance_is_a);
	RESOLVE(gobj, g_object_unref);

	RESOLVE(gtk, gtk_window_get_type);
	RESOLVE(gtk, gtk_application_window_get_type);
	RESOLVE(gtk, gtk_menu_button_get_type);
	RESOLVE(gtk, gtk_popover_menu_get_type);
	RESOLVE(gtk, gtk_popover_menu_bar_get_type);
	RESOLVE(gtk, gtk_window_get_titlebar);
	RESOLVE(gtk, gtk_window_get_child);
	RESOLVE(gtk, gtk_widget_get_first_child);
	RESOLVE(gtk, gtk_widget_get_next_sibling);
	RESOLVE(gtk, gtk_menu_button_get_menu_model);
	RESOLVE(gtk, gtk_menu_button_get_popover);
	RESOLVE(gtk, gtk_popover_menu_get_menu_model);
	RESOLVE(gtk, gtk_popover_menu_bar_get_menu_model);
	RESOLVE(gtk, gtk_window_get_application);
	RESOLVE(gtk, gtk_application_get_menubar);
	RESOLVE(gtk, gtk_application_set_menubar);

	RESOLVE(gio, g_menu_new);
	RESOLVE(gio, g_menu_model_get_n_items);
	RESOLVE(gio, g_menu_model_get_item_link);
	RESOLVE(gio, g_menu_item_new_from_model);
	RESOLVE(gio, g_menu_item_set_link);
	RESOLVE(gio, g_menu_append_item);
	RESOLVE(gio, g_menu_model_get_item_attribute_value);
	RESOLVE(glib, g_variant_unref);
	RESOLVE(gio, g_menu_append_submenu);
	RESOLVE(gio, g_application_get_application_id);
	RESOLVE(gio, g_desktop_app_info_new);
	RESOLVE(gio, g_app_info_get_name);
	RESOLVE(gio, g_settings_schema_source_get_default);
	RESOLVE(gio, g_settings_schema_source_lookup);
	RESOLVE(gio, g_settings_schema_unref);
	RESOLVE(gio, g_settings_new);
	RESOLVE(gio, g_settings_get_boolean);

	RESOLVE(gtk, gtk_widget_get_parent);
	RESOLVE(gtk, gtk_widget_is_visible);
	RESOLVE(gtk, gtk_widget_get_child_visible);
	RESOLVE(gio, g_simple_action_new_stateful);
	RESOLVE(gio, g_simple_action_set_state);
	RESOLVE(gio, g_action_get_state);
	RESOLVE(gobj, g_signal_connect_object);
	RESOLVE(gobj, g_object_class_find_property);
	RESOLVE(gobj, g_object_get_property);
	RESOLVE(gobj, g_value_init);
	RESOLVE(gobj, g_value_unset);
	RESOLVE(gobj, g_value_get_boolean);
	RESOLVE(gobj, g_value_get_int);
	RESOLVE(gobj, g_value_get_uint);
	RESOLVE(gobj, g_value_get_double);
	RESOLVE(gobj, g_value_get_float);
	RESOLVE(gobj, g_value_get_string);
	RESOLVE(gobj, g_value_get_enum);
	RESOLVE(gobj, g_enum_get_value);
	RESOLVE(gobj, g_type_class_unref);
	RESOLVE(gobj, g_type_fundamental);
	RESOLVE(glib, g_variant_new_boolean);
	RESOLVE(glib, g_variant_new_int32);
	RESOLVE(glib, g_variant_new_uint32);
	RESOLVE(glib, g_variant_new_double);
	RESOLVE(glib, g_variant_get_boolean);
	RESOLVE(glib, g_variant_is_of_type);
	RESOLVE(gobj, g_object_set_data);
	RESOLVE(gobj, g_object_get_data);
	RESOLVE(gio, g_simple_action_set_enabled);
	RESOLVE(gtk, gtk_widget_class_query_action);
	RESOLVE(gtk, gtk_widget_activate_action_variant);
	RESOLVE(gio, g_simple_action_new);
	RESOLVE(gio, g_action_map_add_action);
	RESOLVE(gio, g_action_map_lookup_action);
	RESOLVE(gio, g_menu_item_set_attribute_value);
	RESOLVE(gobj, g_signal_connect_data);
	RESOLVE(gobj, g_object_add_weak_pointer);
	RESOLVE(gobj, g_object_remove_weak_pointer);
	RESOLVE(glib, g_variant_get_string);
	RESOLVE(glib, g_variant_new_string);

	RESOLVE(glib, g_get_application_name);

	/* Optional: GTK 4.4. Without it the choice falls back to the score. */
	*(void **)(&p_gtk_menu_button_get_primary) =
		dlsym(gtk, "gtk_menu_button_get_primary");
	RESOLVE(glib, g_strdup);
	RESOLVE(glib, g_strconcat);
	RESOLVE(glib, g_free);

	RESOLVE(gobj, g_object_ref);
	RESOLVE(gobj, g_object_set_data_full);
	RESOLVE(gobj, g_object_weak_ref);
	RESOLVE(gobj, g_object_weak_unref);
	RESOLVE(gobj, g_signal_handler_disconnect);
	RESOLVE(glib, g_idle_add);
	RESOLVE(glib, g_source_remove);
	RESOLVE(gobj, g_type_from_name);
	RESOLVE(gobj, g_type_children);
	RESOLVE(gobj, g_type_class_peek);
	RESOLVE(gio, g_application_get_is_registered);
	RESOLVE(gio, g_application_get_is_remote);
	RESOLVE(gtk, gtk_widget_get_root);
	RESOLVE(gtk, gtk_header_bar_get_type);
	RESOLVE(gtk, gtk_popover_get_type);
	RESOLVE(gio, g_menu_remove);
	RESOLVE(gio, g_menu_insert_item);
	RESOLVE(gio, g_menu_model_iterate_item_attributes);
	RESOLVE(gio, g_menu_attribute_iter_get_next);
	RESOLVE(gio, g_menu_model_iterate_item_links);
	RESOLVE(gio, g_menu_link_iter_get_next);
	RESOLVE(glib, g_variant_equal);
	RESOLVE(gtk, gtk_application_get_windows);

	type_window = p_gtk_window_get_type();
	type_app_window = p_gtk_application_window_get_type();
	type_menu_button = p_gtk_menu_button_get_type();
	type_popover_menu = p_gtk_popover_menu_get_type();
	type_popover_menu_bar = p_gtk_popover_menu_bar_get_type();
	type_header_bar = p_gtk_header_bar_get_type();
	type_popover = p_gtk_popover_get_type();

	return 1;
}

static int is_a(void *instance, GType type)
{
	return instance != NULL &&
	       p_g_type_check_instance_is_a((GTypeInstance *)instance, type);
}

/* ---- finding the menu ---------------------------------------------------- */

/*
 * A window often has several menu buttons: gnome-calculator's mode selector,
 * gnome-text-editor's search options and nautilus's folder menu all come
 * before the main menu in tree order, so taking the first one exported the
 * wrong menu in five of thirteen applications measured.
 *
 * So every candidate is collected and ranked by, in order:
 *  1. shown - visible, and not in a part of the window its container keeps
 *     hidden. Nothing is mapped yet at realize, so gtk_widget_is_visible()
 *     alone says yes to a hidden stack page; a GtkStack, AdwToolbarView or
 *     similar container clears child-visible on what it does not show, so
 *     that is checked on every ancestor too. It matters: libadwaita marks
 *     the menu of its hidden tab overview primary, while gnome-console's real
 *     main menu is not marked at all, and papers keeps a second primary menu
 *     in its document view, hidden until a document is open;
 *  2. primary (gtk_menu_button_set_primary, GTK 4.4) - the button F10 opens,
 *     which libadwaita applications set on the main menu;
 *  3. the number of items naming app. or win. actions. The main menu is made
 *     of those; secondary menus are usually built from a group inserted on
 *     the widget they belong to (search-options., view.);
 *  4. tree order.
 */
#define MAX_CANDIDATES 32

struct candidate {
	GtkWidget *owner; /* the menu button or the popover menu bar */
	GMenuModel *model;
	int shown;
	int primary;
	int score;
};

struct candidates {
	struct candidate c[MAX_CANDIDATES];
	int n;
};

static int reachable_items(GMenuModel *model, int depth);

static int is_shown(GtkWidget *widget)
{
	if (!p_gtk_widget_is_visible(widget))
		return 0;
	for (GtkWidget *w = widget; w != NULL; w = p_gtk_widget_get_parent(w))
		if (!p_gtk_widget_get_child_visible(w))
			return 0;
	return 1;
}

static void add_candidate(struct candidates *out, GtkWidget *owner,
			  GMenuModel *model, int primary, int depth)
{
	if (out->n == MAX_CANDIDATES)
		return;

	struct candidate *c = &out->c[out->n++];
	c->owner = owner;
	c->model = model;
	c->shown = is_shown(owner);
	c->primary = primary;
	c->score = reachable_items(model, 0);
	note("candidate %d at depth %d: shown=%d primary=%d app./win. items=%d",
	     out->n, depth, c->shown, c->primary, c->score);
}

/*
 * How a look at a window is done. At realize (late == 0) every menu button
 * and popover menu bar of the window is a candidate, as in 0.10. A late look
 * (late == 1) is made when a menu button or popover menu bar is mapped in an
 * application window still waiting for a menu, after realize found none
 * (see "menus that appear after realize" below). It is narrower, because by
 * then a dialog presented inside the window (libadwaita's AdwDialog), a list
 * row's own menu button or a popover can have the window as root:
 *  - the owner is in the window's titlebar, or has a GtkHeaderBar or
 *    AdwHeaderBar ancestor;
 *  - it has no AdwDialog, AdwBottomSheet or GtkPopover ancestor;
 *  - it is shown, and its menu reaches at least one app. or win. action.
 * In every look a candidate whose model is or contains our own menubar is
 * skipped, and so is the window's own in-window menubar (a popover menu bar
 * whose parent is the window), which GTK builds from our menubar when the
 * shell does not show menubars.
 */
struct look {
	GtkWindow *window;
	int late;
	int in_titlebar;
	GMenuModel *ours[2]; /* the container and the inner menu, or NULL */
};

/* Adw types exist only once libadwaita has registered them, which in GI and
   .NET applications happens after GTK is hooked; so they are looked up at
   each late look, and only a found type is kept. */
static GType adw_type(GType *cache, const char *name)
{
	if (*cache == 0)
		*cache = p_g_type_from_name(name);
	return *cache;
}

static GType type_adw_header_bar, type_adw_dialog, type_adw_bottom_sheet;

static int late_owner_ok(GtkWidget *owner, const struct look *lk)
{
	GType hb = adw_type(&type_adw_header_bar, "AdwHeaderBar");
	GType dlg = adw_type(&type_adw_dialog, "AdwDialog");
	GType sheet = adw_type(&type_adw_bottom_sheet, "AdwBottomSheet");
	int in_header = lk->in_titlebar;

	for (GtkWidget *w = p_gtk_widget_get_parent(owner);
	     w != NULL && w != (GtkWidget *)lk->window;
	     w = p_gtk_widget_get_parent(w)) {
		if ((dlg != 0 && is_a(w, dlg)) || (sheet != 0 && is_a(w, sheet)) ||
		    is_a(w, type_popover))
			return 0;
		if (is_a(w, type_header_bar) || (hb != 0 && is_a(w, hb)))
			in_header = 1;
	}
	return in_header;
}

/* @model is one of ours, or holds one of ours within two levels of links. */
static int contains_ours(GMenuModel *model, const struct look *lk, int depth)
{
	if (model == NULL)
		return 0;
	if (model == lk->ours[0] || model == lk->ours[1])
		return 1;
	if (depth >= 2)
		return 0;

	gint n = p_g_menu_model_get_n_items(model);
	for (gint i = 0; i < n; i++) {
		const char *links[] = { "section", "submenu" };
		for (int l = 0; l < 2; l++) {
			GMenuModel *child =
				p_g_menu_model_get_item_link(model, i, links[l]);
			if (child == NULL)
				continue;
			int found = contains_ours(child, lk, depth + 1);
			p_g_object_unref(child);
			if (found)
				return 1;
		}
	}
	return 0;
}

static void watch_late(GtkWidget *widget, const char *property,
		       GtkWindow *window);

static void consider(struct candidates *out, GtkWidget *owner,
		     GMenuModel *model, int primary, int depth,
		     const struct look *lk)
{
	if (contains_ours(model, lk, 0))
		return;
	if (lk->late && (!late_owner_ok(owner, lk) || !is_shown(owner) ||
			 reachable_items(model, 0) == 0))
		return;
	add_candidate(out, owner, model, primary, depth);
}

static void collect_menus(GtkWidget *widget, int depth, struct candidates *out,
			  const struct look *lk)
{
	if (widget == NULL || depth > 32)
		return;

	if (is_a(widget, type_menu_button)) {
		GtkMenuButton *button = (GtkMenuButton *)widget;
		GMenuModel *model = p_gtk_menu_button_get_menu_model(button);
		GtkPopover *popover = NULL;

		/* An application may call gtk_menu_button_set_popover() rather
		   than set_menu_model(), leaving get_menu_model() NULL though a
		   menu exists - yelp does this for all four of its buttons.
		   Where the popover is a GtkPopoverMenu the model is still
		   reachable. */
		if (model == NULL) {
			popover = p_gtk_menu_button_get_popover(button);
			if (is_a(popover, type_popover_menu))
				model = p_gtk_popover_menu_get_menu_model(
					(GtkPopoverMenu *)popover);
		}

		if (model != NULL) {
			int primary = p_gtk_menu_button_get_primary != NULL &&
				      p_gtk_menu_button_get_primary(button);
			consider(out, widget, model, primary, depth, lk);
		} else if (lk->late) {
			/* Mapped without a menu yet: look again when it gets
			   one. */
			watch_late(widget, "notify::menu-model", lk->window);
			watch_late(widget, "notify::popover", lk->window);
			if (is_a(popover, type_popover_menu))
				watch_late((GtkWidget *)popover,
					   "notify::menu-model", lk->window);
		}
	}

	if (is_a(widget, type_popover_menu_bar) &&
	    p_gtk_widget_get_parent(widget) != (GtkWidget *)lk->window) {
		GMenuModel *model = p_gtk_popover_menu_bar_get_menu_model(
			(GtkPopoverMenuBar *)widget);
		if (model != NULL)
			consider(out, widget, model, 0, depth, lk);
		else if (lk->late)
			watch_late(widget, "notify::menu-model", lk->window);
	}

	for (GtkWidget *child = p_gtk_widget_get_first_child(widget);
	     child != NULL; child = p_gtk_widget_get_next_sibling(child))
		collect_menus(child, depth + 1, out, lk);
}

/* Items anywhere in @model whose action is app.* or win.* */
static int reachable_items(GMenuModel *model, int depth)
{
	int count = 0;
	gint n = p_g_menu_model_get_n_items(model);

	for (gint i = 0; i < n; i++) {
		GVariant *v = p_g_menu_model_get_item_attribute_value(
			model, i, "action", G_VARIANT_TYPE_STRING);
		if (v != NULL) {
			const char *action = p_g_variant_get_string(v, NULL);
			if (strncmp(action, "app.", 4) == 0 ||
			    strncmp(action, "win.", 4) == 0)
				count++;
			p_g_variant_unref(v);
		}

		const char *links[] = { "section", "submenu" };
		for (int l = 0; l < 2 && depth < 16; l++) {
			GMenuModel *child =
				p_g_menu_model_get_item_link(model, i, links[l]);
			if (child != NULL) {
				count += reachable_items(child, depth + 1);
				p_g_object_unref(child);
			}
		}
	}

	return count;
}

/* On success *owner is the widget the menu hangs off. Its actions resolve the
   way the application's own popover resolves them, which is what activating
   a widget-scoped action needs. */
static GMenuModel *find_menu_model(GtkWindow *window, GtkWidget **owner,
				   struct look *lk)
{
	struct candidates found = { .n = 0 };

	/* The title bar is not part of the ordinary child tree, and yelp keeps
	   its header bar in the content rather than the title bar. Try both. */
	lk->window = window;
	lk->in_titlebar = 1;
	collect_menus(p_gtk_window_get_titlebar(window), 0, &found, lk);
	lk->in_titlebar = 0;
	collect_menus(p_gtk_window_get_child(window), 0, &found, lk);

	if (found.n == 0)
		return NULL;

	int best = 0;
	for (int i = 1; i < found.n; i++) {
		struct candidate *c = &found.c[i], *b = &found.c[best];
		if (c->shown != b->shown) {
			if (c->shown)
				best = i;
		} else if (c->primary != b->primary) {
			if (c->primary)
				best = i;
		} else if (c->score > b->score) {
			best = i;
		}
	}
	note("chose candidate %d of %d", best + 1, found.n);

	*owner = found.c[best].owner;
	return found.c[best].model;
}

/* ---- the label ----------------------------------------------------------- */

static gchar *menu_label(GtkApplication *app)
{
	gboolean use_app_name = TRUE;
	const char *override = getenv("UNITY_GTK4_MENU_LABEL");

	if (override != NULL) {
		use_app_name = (strcmp(override, "generic") != 0);
	} else {
		GSettingsSchemaSource *source =
			p_g_settings_schema_source_get_default();
		GSettingsSchema *schema =
			source ? p_g_settings_schema_source_lookup(
					 source, SHIM_SCHEMA, TRUE)
			       : NULL;
		if (schema != NULL) {
			GSettings *settings = p_g_settings_new(SHIM_SCHEMA);
			use_app_name = p_g_settings_get_boolean(
				settings, "show-application-name");
			p_g_object_unref(settings);
			p_g_settings_schema_unref(schema);
		}
	}

	if (!use_app_name)
		return p_g_strdup("Menu");

	/* Prefer the .desktop Name: that is the field Unity's panel reads
	   through BAMF, so the entry repeats it exactly rather than
	   approximately. Only works when the file is named after the
	   application id, which is the current convention. */
	const char *app_id =
		p_g_application_get_application_id((GApplication *)app);
	if (app_id != NULL) {
		gchar *desktop_id = p_g_strconcat(app_id, ".desktop", NULL);
		GDesktopAppInfo *info = p_g_desktop_app_info_new(desktop_id);
		p_g_free(desktop_id);

		if (info != NULL) {
			const char *name =
				p_g_app_info_get_name((GAppInfo *)info);
			if (name != NULL) {
				gchar *result = p_g_strdup(name);
				p_g_object_unref(info);
				return result;
			}
			p_g_object_unref(info);
		}
	}

	const char *fallback = p_g_get_application_name();
	return p_g_strdup(fallback ? fallback : "Menu");
}

/* ---- widget-scoped actions --------------------------------------------- */

/*
 * The global menu can only activate what the application exports over D-Bus:
 * the application's action group ("app.") and a GtkApplicationWindow's own
 * action map ("win."). A header bar menu may also name actions installed on a
 * widget class with gtk_widget_class_install_action() - yelp's "About Help" is
 * win.yelp-show-about-dialog, a class action of YelpWindow, not an entry in the
 * window's action map. Inside the application the popover finds it through the
 * widget's action muxer; over D-Bus nobody can, so Unity showed it greyed out.
 *
 * For each such item the exported copy of the model points at a stand-in added
 * to the window's action map, and the stand-in activates the original name
 * from the menu's owner widget - the same lookup the popover itself performs.
 * Only class actions positively found on the owner or its ancestors get one.
 * A property action (gtk_widget_class_install_property_action) gets a
 * stateful stand-in that mirrors the property - a check mark for a boolean,
 * the selected radio item otherwise; see "property actions" below. Names
 * with a prefix other than app. or win. usually come from a group inserted on
 * a sub-widget, which public API cannot enumerate; they are left alone and
 * logged.
 *
 * GTK has no public getter for a class action's enabled state, only the
 * setter gtk_widget_action_set_enabled(). So the setter is intercepted (see
 * the end of this file), every call is remembered on the widget, and a
 * stand-in takes its enabled state from there when created and follows it
 * afterwards. Calls GTK makes internally do not pass through it; those are
 * GTK's own widgets' actions, which header bar menus do not use.
 */
#define PROXY_PREFIX "unity-gtk4-menu-"

struct proxy {
	GtkWidget *owner;  /* weak; the menu button, activation starts here */
	GtkWidget *holder; /* weak; the widget whose class has the action */
	gchar *name;
};

#define ENABLED_KEY "unity-gtk4-menu-enabled:"
#define PROXY_KEY "unity-gtk4-menu-proxy:"

static void make_key(char *buf, size_t size, const char *prefix,
		     const char *name)
{
	snprintf(buf, size, "%s%s", prefix, name);
}

static void proxy_activate(GSimpleAction *action, GVariant *parameter,
			   gpointer data)
{
	struct proxy *proxy = data;

	if (proxy->owner == NULL)
		return;

	if (!p_gtk_widget_activate_action_variant(proxy->owner, proxy->name,
						  parameter))
		note("%s did not resolve from its owner", proxy->name);
}

static void proxy_free(gpointer data, GClosure *closure)
{
	struct proxy *proxy = data;

	if (proxy->owner != NULL)
		p_g_object_remove_weak_pointer((GObject *)proxy->owner,
					       (gpointer *)&proxy->owner);
	if (proxy->holder != NULL) {
		char key[256];
		make_key(key, sizeof key, PROXY_KEY, proxy->name);
		p_g_object_set_data((GObject *)proxy->holder, key, NULL);
		p_g_object_remove_weak_pointer((GObject *)proxy->holder,
					       (gpointer *)&proxy->holder);
	}
	p_g_free(proxy->name);
	free(proxy);
}

/* Returns the widget, @widget or an ancestor, whose class has @name as an
   action, or NULL. *property is set to the property a property action is
   bound to, NULL for an ordinary one. */
static GtkWidget *find_class_action(GtkWidget *widget, const char *name,
				    const GVariantType **parameter_type,
				    const char **property)
{
	for (; widget != NULL; widget = p_gtk_widget_get_parent(widget)) {
		GtkWidgetClass *klass =
			(GtkWidgetClass *)((GTypeInstance *)widget)->g_class;
		GType owner_type;
		const char *action_name, *property_name;
		const GVariantType *ptype;

		for (guint i = 0; p_gtk_widget_class_query_action(
			     klass, i, &owner_type, &action_name, &ptype,
			     &property_name);
		     i++) {
			if (strcmp(action_name, name) != 0)
				continue;
			*parameter_type = ptype;
			*property = property_name;
			return widget;
		}
	}
	return NULL;
}

/*
 * Property actions. GTK derives the action from the property's type
 * (gtkwidget.c determine_type): a boolean is a toggle with no parameter, an
 * int, uint, float, double, string or enum is set by a parameter of the same
 * type - an enum as its nick - and the state is the current value. The
 * stand-in is a stateful GSimpleAction carrying that state. Activating it
 * goes through the owner widget like any other stand-in, so GTK sets the
 * property; the stand-in's state follows the property's notify signal.
 */
static GVariant *property_state(GObject *object, GParamSpec *pspec)
{
	GValue value = G_VALUE_INIT;
	GVariant *state = NULL;
	GType type = pspec->value_type;

	p_g_value_init(&value, type);
	p_g_object_get_property(object, pspec->name, &value);

	if (type == G_TYPE_BOOLEAN) {
		state = p_g_variant_new_boolean(p_g_value_get_boolean(&value));
	} else if (type == G_TYPE_INT) {
		state = p_g_variant_new_int32(p_g_value_get_int(&value));
	} else if (type == G_TYPE_UINT) {
		state = p_g_variant_new_uint32(p_g_value_get_uint(&value));
	} else if (type == G_TYPE_DOUBLE) {
		state = p_g_variant_new_double(p_g_value_get_double(&value));
	} else if (type == G_TYPE_FLOAT) {
		state = p_g_variant_new_double(p_g_value_get_float(&value));
	} else if (type == G_TYPE_STRING) {
		const char *str = p_g_value_get_string(&value);
		state = p_g_variant_new_string(str != NULL ? str : "");
	} else if (p_g_type_fundamental(type) == G_TYPE_ENUM) {
		GEnumClass *klass = p_g_type_class_ref(type);
		GEnumValue *ev = p_g_enum_get_value(klass,
						    p_g_value_get_enum(&value));
		state = p_g_variant_new_string(ev != NULL ? ev->value_nick : "");
		p_g_type_class_unref(klass);
	}

	p_g_value_unset(&value);
	return state; /* floating, or NULL for a type GTK itself rejects */
}

static void property_notified(GObject *holder, GParamSpec *pspec,
			      gpointer action)
{
	GVariant *state = property_state(holder, pspec);

	if (state != NULL)
		p_g_simple_action_set_state(action, state);
}

/* A change of state asked for over D-Bus (org.gtk.Actions.SetState). Left to
   itself GSimpleAction would just store it; route it to the property the same
   way activation does, and let the notify bring the new state back. */
static void proxy_change_state(GSimpleAction *action, GVariant *value,
			       gpointer data)
{
	struct proxy *proxy = data;

	if (proxy->owner == NULL)
		return;

	if (p_g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN)) {
		GVariant *now = p_g_action_get_state((GAction *)action);
		gboolean differs = now == NULL ||
			p_g_variant_get_boolean(now) != p_g_variant_get_boolean(value);
		if (now != NULL)
			p_g_variant_unref(now);
		if (differs)
			p_gtk_widget_activate_action_variant(proxy->owner,
							     proxy->name, NULL);
	} else {
		p_gtk_widget_activate_action_variant(proxy->owner, proxy->name,
						     value);
	}
}

/* The name the exported item should use for @name: a stand-in in @map when
   @name is a class action reachable from @owner, otherwise NULL - keep it. */
static gchar *proxy_for(const char *name, GtkWidget *owner, GActionMap *map)
{
	const GVariantType *ptype = NULL;
	const char *property = NULL;
	GtkWidget *holder = find_class_action(owner, name, &ptype, &property);

	if (holder == NULL) {
		if (strncmp(name, "app.", 4) != 0 &&
		    strncmp(name, "win.", 4) != 0)
			note("%s has a prefix the global menu cannot reach",
			     name);
		return NULL;
	}

	/* No dots: the whole name after "win." is one action name, and a dot
	   inside it would only invite a parser to split it. */
	gchar *local = p_g_strconcat(PROXY_PREFIX, name, NULL);
	for (gchar *c = local; *c; c++)
		if (*c == '.')
			*c = '-';

	if (p_g_action_map_lookup_action(map, local) == NULL) {
		struct proxy *proxy = calloc(1, sizeof *proxy);
		proxy->owner = owner;
		proxy->holder = holder;
		proxy->name = p_g_strdup(name);
		p_g_object_add_weak_pointer((GObject *)owner,
					    (gpointer *)&proxy->owner);
		p_g_object_add_weak_pointer((GObject *)holder,
					    (gpointer *)&proxy->holder);

		GParamSpec *pspec = NULL;
		GVariant *state = NULL;
		if (property != NULL) {
			pspec = p_g_object_class_find_property(
				(GObjectClass *)((GTypeInstance *)holder)->g_class,
				property);
			if (pspec != NULL)
				state = property_state((GObject *)holder, pspec);
			if (state == NULL) {
				note("%s: property %s has no usable state, not proxied",
				     name, property);
				p_g_object_remove_weak_pointer(
					(GObject *)owner, (gpointer *)&proxy->owner);
				p_g_object_remove_weak_pointer(
					(GObject *)holder, (gpointer *)&proxy->holder);
				p_g_free(proxy->name);
				free(proxy);
				p_g_free(local);
				return NULL;
			}
		}

		GSimpleAction *action =
			state != NULL
				? p_g_simple_action_new_stateful(local, ptype, state)
				: p_g_simple_action_new(local, ptype);
		p_g_signal_connect_data(action, "activate",
					(GCallback)proxy_activate, proxy,
					proxy_free, 0);
		if (state != NULL) {
			p_g_signal_connect_data(action, "change-state",
						(GCallback)proxy_change_state,
						proxy, NULL, 0);
			/* Disconnected by GObject when the action goes. */
			char signal[256];
			snprintf(signal, sizeof signal, "notify::%s", pspec->name);
			p_g_signal_connect_object(holder, signal,
						  (GCallback)property_notified,
						  action, 0);
		}

		/* Start from whatever the application has set so far. */
		char key[256];
		make_key(key, sizeof key, ENABLED_KEY, name);
		gboolean enabled = p_g_object_get_data((GObject *)holder, key) !=
				   GINT_TO_POINTER(2);
		p_g_simple_action_set_enabled(action, enabled);

		make_key(key, sizeof key, PROXY_KEY, name);
		p_g_object_set_data((GObject *)holder, key, action);

		p_g_action_map_add_action(map, (GAction *)action);
		p_g_object_unref(action);
		note("%s proxied as win.%s%s%s", name, local,
		     state != NULL ? " with state" : "",
		     enabled ? "" : ", disabled");
	}

	gchar *exported = p_g_strconcat("win.", local, NULL);
	p_g_free(local);
	return exported;
}

/* What clean_model needs to proxy actions: the widget the menu came from and
   the window's action map, or NULL map when the window exports none. */
struct scope {
	GtkWidget *owner;
	GActionMap *map;
	int dropped;
};

static void proxy_item(GMenuModel *model, gint i, GMenuItem *item,
		       struct scope *scope)
{
	if (scope->map == NULL)
		return;

	GVariant *v = p_g_menu_model_get_item_attribute_value(
		model, i, "action", G_VARIANT_TYPE_STRING);
	if (v == NULL)
		return;

	gchar *exported = proxy_for(p_g_variant_get_string(v, NULL),
				    scope->owner, scope->map);
	if (exported != NULL) {
		p_g_menu_item_set_attribute_value(
			item, "action", p_g_variant_new_string(exported));
		p_g_free(exported);
	}
	p_g_variant_unref(v);
}

/* ---- cleaning the model ------------------------------------------------- */

/*
 * A GtkPopoverMenu can embed widgets: an item carrying a "custom" attribute is
 * a slot where the application puts a live widget - yelp's zoom controls, for
 * instance. A widget cannot cross D-Bus, so exported as-is the slot arrives in
 * the global menu as an empty, nameless row.
 *
 * Build a copy of the model with those items removed, and with any section or
 * submenu left empty by the removal dropped too, so no blank rows or empty
 * separators remain. Every GMenu of the copy carries COPY_KEY, so later edits
 * (sync_menu) can tell our menus from the application's.
 */
#define COPY_KEY "unity-gtk4-menu-copy"
#define MAX_DEPTH 16 /* the depth clean_model copies to */

static int is_copy(GMenuModel *model)
{
	return model != NULL && p_g_object_get_data((GObject *)model, COPY_KEY);
}

static int has_attribute(GMenuModel *model, gint i, const char *name)
{
	GVariant *v = p_g_menu_model_get_item_attribute_value(model, i, name, NULL);
	if (v == NULL)
		return 0;
	p_g_variant_unref(v);
	return 1;
}

static GMenuModel *clean_model(GMenuModel *model, int depth,
			       struct scope *scope)
{
	GMenu *out = p_g_menu_new();
	gint n = p_g_menu_model_get_n_items(model);

	p_g_object_set_data((GObject *)out, COPY_KEY, GINT_TO_POINTER(1));

	for (gint i = 0; i < n; i++) {
		if (has_attribute(model, i, "custom")) {
			scope->dropped++;
			continue;
		}

		GMenuItem *item = p_g_menu_item_new_from_model(model, i);
		int keep = 1;

		proxy_item(model, i, item, scope);

		const char *links[] = { "section", "submenu" };
		for (int l = 0; l < 2 && depth < MAX_DEPTH; l++) {
			GMenuModel *child =
				p_g_menu_model_get_item_link(model, i, links[l]);
			if (child == NULL)
				continue;

			GMenuModel *cleaned = clean_model(child, depth + 1, scope);
			p_g_object_unref(child);

			if (p_g_menu_model_get_n_items(cleaned) == 0)
				keep = 0;
			else
				p_g_menu_item_set_link(item, links[l], cleaned);
			p_g_object_unref(cleaned);
		}

		if (keep)
			p_g_menu_append_item(out, item);
		else
			scope->dropped++;
		p_g_object_unref(item);
	}

	return (GMenuModel *)out;
}

/* ---- attaching ----------------------------------------------------------- */

/*
 * The menubar we set is a record kept on the GtkApplication, in one of three
 * states (no record means nothing was set yet):
 *  OURS_EMPTY  our menubar is set and has no item yet: no window had a menu
 *              at realize. GTK writes _GTK_MENUBAR_OBJECT_PATH only at
 *              realize and only when a menubar is set then, so the menubar
 *              must exist before a menu does.
 *  OURS_FILLED it holds one item, the holder labelled with the application's
 *              name, whose submenu is the cleaned copy of the menu found.
 *  TAKEN       the application set a menubar of its own, at realize or later.
 *              Final: we never set ours again.
 * container is the menubar and inner the holder's submenu. Both are edited in
 * place, never replaced, so a panel that subscribed to the empty menubar sees
 * the holder appear.
 */
enum { OURS_EMPTY = 1, OURS_FILLED, TAKEN };

struct watch {
	GMenuModel *model;
	gulong handler;
};

struct record {
	GtkApplication *app; /* the record lives as long as the application */
	int state;
	GMenu *container;
	GMenu *inner;
	/* following the menu copied into inner, from the fill on */
	GMenuModel *source;
	GtkWidget *owner;   /* weak */
	GtkWindow *window;  /* weak */
	struct watch *watches;
	int n_watches, max_watches;
	char **names;       /* the source's action names at the last copy */
	int n_names;
	guint rebuild_idle;
	int syncing;
	unsigned rebuilds;
};

#define RECORD_KEY "unity-gtk4-menu-record"

/* ---- following changes of the menu copied --------------------------------- */

/*
 * After a fill the record keeps the source model and watches items-changed on
 * it and on every section and submenu down to the depth clean_model copies
 * (MAX_DEPTH); below it our copy links the application's own models, which
 * the exporter watches itself. A burst of changes
 * schedules one idle, which copies the source again and syncs the copy into
 * inner in place (sync_menu), so the exporter sends Changed only for what
 * changed and an open panel menu keeps its groups. Our own menus are never
 * watched, and the application's are never edited.
 */
static void on_source_changed(GMenuModel *model, gint position, gint removed,
			      gint added, gpointer data);

static int watched(struct record *rec, GMenuModel *model)
{
	for (int i = 0; i < rec->n_watches; i++)
		if (rec->watches[i].model == model)
			return 1;
	return 0;
}

static void watch_model(struct record *rec, GMenuModel *model, int depth)
{
	if (depth > MAX_DEPTH || model == (GMenuModel *)rec->container ||
	    model == (GMenuModel *)rec->inner || watched(rec, model))
		return;

	if (rec->n_watches == rec->max_watches) {
		int max = rec->max_watches ? rec->max_watches * 2 : 8;
		struct watch *w = realloc(rec->watches, max * sizeof *w);
		if (w == NULL)
			return;
		rec->watches = w;
		rec->max_watches = max;
	}
	struct watch *w = &rec->watches[rec->n_watches++];
	w->model = p_g_object_ref(model);
	w->handler = p_g_signal_connect_data(model, "items-changed",
					     (GCallback)on_source_changed, rec,
					     NULL, 0);

	gint n = p_g_menu_model_get_n_items(model);
	for (gint i = 0; i < n; i++) {
		const char *links[] = { "section", "submenu" };
		for (int l = 0; l < 2; l++) {
			GMenuModel *child =
				p_g_menu_model_get_item_link(model, i, links[l]);
			if (child == NULL)
				continue;
			watch_model(rec, child, depth + 1);
			p_g_object_unref(child);
		}
	}
}

static void unwatch(struct record *rec)
{
	for (int i = 0; i < rec->n_watches; i++) {
		p_g_signal_handler_disconnect(rec->watches[i].model,
					      rec->watches[i].handler);
		p_g_object_unref(rec->watches[i].model);
	}
	rec->n_watches = 0;
}

static int has_name(char **names, int n, const char *name)
{
	for (int i = 0; i < n; i++)
		if (strcmp(names[i], name) == 0)
			return 1;
	return 0;
}

static void collect_names(GMenuModel *model, int depth, char ***names, int *n)
{
	if (depth > MAX_DEPTH)
		return;

	gint items = p_g_menu_model_get_n_items(model);
	for (gint i = 0; i < items; i++) {
		GVariant *v = p_g_menu_model_get_item_attribute_value(
			model, i, "action", G_VARIANT_TYPE_STRING);
		if (v != NULL) {
			const char *name = p_g_variant_get_string(v, NULL);
			char **more = has_name(*names, *n, name)
					      ? NULL
					      : realloc(*names, (*n + 1) * sizeof **names);
			if (more != NULL) {
				*names = more;
				(*names)[*n] = strdup(name);
				if ((*names)[*n] != NULL)
					(*n)++;
			}
			p_g_variant_unref(v);
		}
		const char *links[] = { "section", "submenu" };
		for (int l = 0; l < 2; l++) {
			GMenuModel *child =
				p_g_menu_model_get_item_link(model, i, links[l]);
			if (child == NULL)
				continue;
			collect_names(child, depth + 1, names, n);
			p_g_object_unref(child);
		}
	}
}

static void free_names(char **names, int n)
{
	for (int i = 0; i < n; i++)
		free(names[i]);
	free(names);
}

static void stop_following(struct record *rec)
{
	if (rec->rebuild_idle != 0)
		p_g_source_remove(rec->rebuild_idle);
	rec->rebuild_idle = 0;
	unwatch(rec);
	free(rec->watches);
	rec->watches = NULL;
	rec->max_watches = 0;
	if (rec->owner != NULL)
		p_g_object_remove_weak_pointer((GObject *)rec->owner,
					       (gpointer *)&rec->owner);
	if (rec->window != NULL)
		p_g_object_remove_weak_pointer((GObject *)rec->window,
					       (gpointer *)&rec->window);
	rec->owner = NULL;
	rec->window = NULL;
	if (rec->source != NULL)
		p_g_object_unref(rec->source);
	rec->source = NULL;
	free_names(rec->names, rec->n_names);
	rec->names = NULL;
	rec->n_names = 0;
}

static void start_following(struct record *rec, GMenuModel *source,
			    GtkWidget *owner, GtkWindow *window)
{
	stop_following(rec);
	rec->source = p_g_object_ref(source);
	rec->owner = owner;
	rec->window = window;
	if (owner != NULL)
		p_g_object_add_weak_pointer((GObject *)owner,
					    (gpointer *)&rec->owner);
	p_g_object_add_weak_pointer((GObject *)window, (gpointer *)&rec->window);
	collect_names(source, 0, &rec->names, &rec->n_names);
	watch_model(rec, source, 0);
}

/* Same attributes (names and values) and the same link names. */
static int same_item(GMenuModel *a, gint i, GMenuModel *b, gint j)
{
	int na = 0, same = 1;
	const gchar *name;
	GVariant *value;

	GMenuAttributeIter *it = p_g_menu_model_iterate_item_attributes(a, i);
	while (same && p_g_menu_attribute_iter_get_next(it, &name, &value)) {
		GVariant *other =
			p_g_menu_model_get_item_attribute_value(b, j, name, NULL);
		same = other != NULL && p_g_variant_equal(value, other);
		if (other != NULL)
			p_g_variant_unref(other);
		p_g_variant_unref(value);
		na++;
	}
	p_g_object_unref(it);
	if (!same)
		return 0;

	int nb = 0;
	it = p_g_menu_model_iterate_item_attributes(b, j);
	while (p_g_menu_attribute_iter_get_next(it, &name, &value)) {
		p_g_variant_unref(value);
		nb++;
	}
	p_g_object_unref(it);
	if (na != nb)
		return 0;

	GMenuModel *link;
	int la = 0, lb = 0;
	GMenuLinkIter *li = p_g_menu_model_iterate_item_links(a, i);
	while (same && p_g_menu_link_iter_get_next(li, &name, &link)) {
		GMenuModel *other = p_g_menu_model_get_item_link(b, j, name);
		same = other != NULL;
		if (other != NULL)
			p_g_object_unref(other);
		p_g_object_unref(link);
		la++;
	}
	p_g_object_unref(li);
	if (!same)
		return 0;
	li = p_g_menu_model_iterate_item_links(b, j);
	while (p_g_menu_link_iter_get_next(li, &name, &link)) {
		p_g_object_unref(link);
		lb++;
	}
	p_g_object_unref(li);
	return la == lb;
}

static void replace_item(GMenu *ours, gint i, GMenuModel *new, gint j)
{
	GMenuItem *item = p_g_menu_item_new_from_model(new, j);
	p_g_menu_remove(ours, i);
	p_g_menu_insert_item(ours, i, item);
	p_g_object_unref(item);
}

static int sync_menu(GMenu *ours, GMenuModel *new, int depth);

/* An item kept at @i of ours (equal to @j of new): its links are synced into
   our own copies, or the item is replaced where a link is the application's
   own model and differs. */
static int sync_links(GMenu *ours, gint i, GMenuModel *new, gint j, int depth)
{
	int replaced = 0, replace = 0;
	const gchar *name;
	GMenuModel *link;

	GMenuLinkIter *li =
		p_g_menu_model_iterate_item_links((GMenuModel *)ours, i);
	while (!replace && p_g_menu_link_iter_get_next(li, &name, &link)) {
		GMenuModel *other = p_g_menu_model_get_item_link(new, j, name);
		if (other != NULL && other != link) {
			if (is_copy(link) && is_copy(other))
				replaced += sync_menu((GMenu *)link, other, depth + 1);
			else
				replace = 1;
		}
		if (other != NULL)
			p_g_object_unref(other);
		p_g_object_unref(link);
	}
	p_g_object_unref(li);

	if (replace) {
		replace_item(ours, i, new, j);
		replaced++;
	}
	return replaced;
}

/* Edit @ours, one of our copies, into @new, another: the common prefix and
   suffix are kept (their links synced), and only the middle is edited. */
static int sync_menu(GMenu *ours, GMenuModel *new, int depth)
{
	GMenuModel *old = (GMenuModel *)ours;
	gint n_old = p_g_menu_model_get_n_items(old);
	gint n_new = p_g_menu_model_get_n_items(new);
	gint min = n_old < n_new ? n_old : n_new;
	gint pre = 0, suf = 0;
	int replaced = 0;

	if (depth > MAX_DEPTH)
		return 0;

	while (pre < min && same_item(old, pre, new, pre))
		pre++;
	while (suf < min - pre &&
	       same_item(old, n_old - 1 - suf, new, n_new - 1 - suf))
		suf++;

	/* The suffix first: edits in the middle shift its positions. */
	for (gint k = 0; k < suf; k++)
		replaced += sync_links(ours, n_old - 1 - k, new, n_new - 1 - k,
				       depth);
	for (gint k = 0; k < pre; k++)
		replaced += sync_links(ours, k, new, k, depth);

	gint mid_old = n_old - pre - suf, mid_new = n_new - pre - suf;
	if (mid_old == mid_new) {
		for (gint k = pre; k < pre + mid_old; k++) {
			if (same_item(old, k, new, k)) {
				replaced += sync_links(ours, k, new, k, depth);
			} else {
				replace_item(ours, k, new, k);
				replaced++;
			}
		}
	} else {
		for (gint k = pre + mid_old - 1; k >= pre; k--)
			p_g_menu_remove(ours, k);
		for (gint k = 0; k < mid_new; k++) {
			GMenuItem *item = p_g_menu_item_new_from_model(new, pre + k);
			p_g_menu_insert_item(ours, pre + k, item);
			p_g_object_unref(item);
		}
		replaced += mid_old > mid_new ? mid_old : mid_new;
	}
	return replaced;
}

/* Stand-ins in every other application window for action names new since the
   last copy, each against that window's own menu widget. */
static void add_stand_ins(struct record *rec, char **names, int n)
{
	/* Most changes relabel or toggle items, and bring no new action:
	   then no other window is looked at. */
	int fresh = 0;
	for (int i = 0; i < n && !fresh; i++)
		fresh = !has_name(rec->names, rec->n_names, names[i]);
	if (!fresh)
		return;

	for (GList *l = p_gtk_application_get_windows(rec->app); l != NULL;
	     l = l->next) {
		GtkWindow *window = l->data;
		if (window == rec->window || !is_a(window, type_app_window))
			continue;

		struct look lk = {
			.late = 0,
			.ours = { (GMenuModel *)rec->container,
				  (GMenuModel *)rec->inner },
		};
		GtkWidget *owner = NULL;
		if (find_menu_model(window, &owner, &lk) == NULL)
			continue;
		for (int i = 0; i < n; i++)
			if (!has_name(rec->names, rec->n_names, names[i]))
				p_g_free(proxy_for(names[i], owner,
						   (GActionMap *)window));
	}
}

static gboolean rebuild_idle(gpointer data)
{
	struct record *rec = data;

	rec->rebuild_idle = 0;
	if (rec->state != OURS_FILLED || rec->source == NULL)
		return FALSE;
	if (rec->window == NULL || rec->owner == NULL) {
		note("the menu's window or widget is gone; the menu is no longer followed");
		stop_following(rec);
		return FALSE;
	}

	GtkApplication *app = p_g_object_ref(rec->app);
	struct scope scope = {
		.owner = rec->owner,
		.map = is_a(rec->window, type_app_window)
			       ? (GActionMap *)rec->window
			       : NULL,
	};

	unwatch(rec);
	GMenuModel *copy = clean_model(rec->source, 0, &scope);
	rec->syncing = 1;
	int replaced = sync_menu(rec->inner, copy, 0);
	rec->syncing = 0;
	p_g_object_unref(copy);

	char **names = NULL;
	int n = 0;
	collect_names(rec->source, 0, &names, &n);
	add_stand_ins(rec, names, n);
	free_names(rec->names, rec->n_names);
	rec->names = names;
	rec->n_names = n;

	watch_model(rec, rec->source, 0);
	rec->rebuilds++;
	note("menu changed (rebuild %u): %d item(s) replaced", rec->rebuilds,
	     replaced);
	p_g_object_unref(app);
	return FALSE;
}

static void on_source_changed(GMenuModel *model, gint position, gint removed,
			      gint added, gpointer data)
{
	struct record *rec = data;

	if (!rec->syncing && rec->rebuild_idle == 0)
		rec->rebuild_idle = p_g_idle_add(rebuild_idle, rec);
}

static void release_menus(struct record *rec)
{
	if (rec->container != NULL)
		p_g_object_unref(rec->container);
	if (rec->inner != NULL)
		p_g_object_unref(rec->inner);
	rec->container = NULL;
	rec->inner = NULL;
}

static void record_free(gpointer data)
{
	struct record *rec = data;

	stop_following(rec);
	release_menus(rec);
	free(rec);
}

static struct record *record_for(GtkApplication *app)
{
	return p_g_object_get_data((GObject *)app, RECORD_KEY);
}

static struct record *record_new(GtkApplication *app)
{
	struct record *rec = calloc(1, sizeof *rec);

	if (rec == NULL)
		return NULL;
	rec->app = app;
	p_g_object_set_data_full((GObject *)app, RECORD_KEY, rec, record_free);
	return rec;
}

/* ---- windows that wait for a menu ------------------------------------------ */

/*
 * An application window whose realize found no menu waits, as long as its
 * application's menubar is still empty. A late look is made at it when a menu
 * button or popover menu bar is mapped in it (the map shims below), or when a
 * button it holds gets a menu later. A window stops waiting when the menubar
 * is filled (from it or from another window), when it is unrealized or
 * destroyed, or when the application takes over the menubar. There is no
 * timer.
 *
 * gtk_window_dispose() releases the application before the window is
 * unrealized and destroyed, so each entry holds a reference on the
 * application, and on the record through it, for as long as it waits.
 */
struct waiter {
	struct waiter *next;
	GtkWindow *window; /* weak */
	GtkApplication *app; /* strong */
	struct record *rec;
	guint idle;
	gulong on_unrealize, on_destroy;
};

static struct waiter *waiters;
static int waiting_count; /* in the whole process, for the map shims */

static struct waiter *waiter_find(GtkWindow *window)
{
	for (struct waiter *w = waiters; w != NULL; w = w->next)
		if (w->window == window && window != NULL)
			return w;
	return NULL;
}

static void waiter_remove(struct waiter *w)
{
	for (struct waiter **p = &waiters; *p != NULL; p = &(*p)->next) {
		if (*p == w) {
			*p = w->next;
			break;
		}
	}
	waiting_count--;

	if (w->idle != 0)
		p_g_source_remove(w->idle);
	if (w->window != NULL) {
		p_g_signal_handler_disconnect(w->window, w->on_unrealize);
		p_g_signal_handler_disconnect(w->window, w->on_destroy);
		p_g_object_remove_weak_pointer((GObject *)w->window,
					       (gpointer *)&w->window);
	}

	GtkApplication *app = w->app;
	free(w);
	note("a window stopped waiting (%d waiting)", waiting_count);
	p_g_object_unref(app); /* last: it may free the record */
}

/* The caller holds a reference on the application. */
static void waiters_drop_record(struct record *rec)
{
	struct waiter *w;

	do {
		for (w = waiters; w != NULL && w->rec != rec; w = w->next)
			;
		if (w != NULL)
			waiter_remove(w);
	} while (w != NULL);
}

static void on_window_gone(GtkWidget *window, gpointer data)
{
	waiter_remove(data);
}

static void go_taken(struct record *rec)
{
	note("the application set a menubar of its own; ours is released");
	rec->state = TAKEN;
	waiters_drop_record(rec);
	stop_following(rec);
	release_menus(rec);
}

static void on_menubar_notify(GObject *app, GParamSpec *pspec, gpointer data)
{
	struct record *rec = data;

	if ((rec->state == OURS_EMPTY || rec->state == OURS_FILLED) &&
	    p_gtk_application_get_menubar((GtkApplication *)app) !=
		    (GMenuModel *)rec->container)
		go_taken(rec);
}

/* ---- looking for a menu and filling the menubar ---------------------------- */

/* Returns 1 if @window's menu filled the menubar. The caller holds a
   reference on the application. */
static int look(GtkWindow *window, struct record *rec, int late)
{
	GtkApplication *app = rec->app;
	struct look lk = {
		.late = late,
		.ours = { (GMenuModel *)rec->container, (GMenuModel *)rec->inner },
	};
	GtkWidget *owner = NULL;
	GMenuModel *model = find_menu_model(window, &owner, &lk);

	if (model == NULL) {
		note(late ? "late look: no menu yet" : "no menu model in this window");
		return 0;
	}

	struct scope scope = {
		.owner = owner,
		.map = is_a(window, type_app_window) ? (GActionMap *)window
						     : NULL,
	};
	GMenuModel *cleaned = clean_model(model, 0, &scope);

	if (scope.dropped > 0)
		note("dropped %d item(s) that cannot cross D-Bus", scope.dropped);

	gint n = p_g_menu_model_get_n_items(cleaned);
	if (n == 0) {
		note("the menu found has no item that can cross D-Bus; not used");
		p_g_object_unref(cleaned);
		return 0;
	}

	/* Nothing scheduled by the fill itself may start another look. */
	waiters_drop_record(rec);

	for (gint i = 0; i < n; i++) {
		GMenuItem *item = p_g_menu_item_new_from_model(cleaned, i);
		p_g_menu_append_item(rec->inner, item);
		p_g_object_unref(item);
	}
	p_g_object_unref(cleaned);
	start_following(rec, model, owner, window);

	gchar *label = menu_label(app);
	p_g_menu_append_submenu(rec->container, label, (GMenuModel *)rec->inner);
	rec->state = OURS_FILLED;
	note("menubar filled%s, labelled \"%s\"", late ? " late" : "", label);
	p_g_free(label);
	return 1;
}

static gboolean look_idle(gpointer data)
{
	struct waiter *w = data;

	w->idle = 0;
	if (w->window == NULL) {
		waiter_remove(w);
		return FALSE;
	}

	GtkApplication *app = p_g_object_ref(w->app);
	struct record *rec = w->rec;

	note("late look");
	if (rec->state == OURS_EMPTY) {
		if (p_gtk_application_get_menubar(app) != (GMenuModel *)rec->container)
			go_taken(rec);
		else
			look(w->window, rec, 1); /* may free w */
	}
	p_g_object_unref(app);
	return FALSE;
}

static void schedule_look(struct waiter *w)
{
	if (w->idle == 0)
		w->idle = p_g_idle_add(look_idle, w);
}

static void on_late_notify(GObject *object, GParamSpec *pspec, gpointer window)
{
	struct waiter *w = waiter_find(window);

	if (w != NULL)
		schedule_look(w);
}

/* A widget of a waiting window that has no menu yet: look again when its
   @property changes. Connected once per widget and property, for the
   window's life. */
static void watch_late(GtkWidget *widget, const char *property,
		       GtkWindow *window)
{
	gchar *key = p_g_strconcat("unity-gtk4-menu-watch:", property, NULL);

	if (p_g_object_get_data((GObject *)widget, key) == NULL) {
		p_g_object_set_data((GObject *)widget, key, (gpointer)1);
		p_g_signal_connect_object(widget, property,
					  G_CALLBACK(on_late_notify), window, 0);
	}
	p_g_free(key);
}

/* ---- the map shims ----------------------------------------------------------- */

/*
 * The class map of GtkMenuButton and GtkPopoverMenuBar is replaced, as the
 * realize of the windows is, once the first window waits; while no window
 * waits the shim costs one integer test. Pinta packs its menu buttons into a
 * header bar that is already mapped, which maps them on the spot with their
 * menu set. The class reference is taken then, not at load (GTK is up by then)
 * and never dropped. Subclasses whose class was initialised before and kept
 * the original map get the shim too; a subclass with its own map is not
 * covered.
 */
static void (*real_menu_button_map)(GtkWidget *);
static void (*real_menu_bar_map)(GtkWidget *);

static void late_mapped(GtkWidget *widget)
{
	if (waiting_count == 0)
		return;

	GtkRoot *root = p_gtk_widget_get_root(widget);
	if (root == NULL || p_gtk_widget_get_parent(widget) == (GtkWidget *)root)
		return; /* the window's own in-window menubar */

	struct waiter *w = waiter_find((GtkWindow *)root);
	if (w != NULL)
		schedule_look(w);
}

static void shim_menu_button_map(GtkWidget *widget)
{
	if (real_menu_button_map != NULL)
		real_menu_button_map(widget);
	late_mapped(widget);
}

static void shim_menu_bar_map(GtkWidget *widget)
{
	if (real_menu_bar_map != NULL)
		real_menu_bar_map(widget);
	late_mapped(widget);
}

static void patch_children(GType type, void (*orig)(GtkWidget *),
			   void (*shim)(GtkWidget *))
{
	guint n = 0;
	GType *children = p_g_type_children(type, &n);

	for (guint i = 0; i < n; i++) {
		GtkWidgetClass *klass = p_g_type_class_peek(children[i]);
		if (klass != NULL && klass->map == orig)
			klass->map = shim;
		patch_children(children[i], orig, shim);
	}
	p_g_free(children);
}

static void patch_map(GType type, void (**real)(GtkWidget *),
		      void (*shim)(GtkWidget *), const char *name)
{
	GtkWidgetClass *klass = (GtkWidgetClass *)p_g_type_class_ref(type);

	if (klass == NULL || klass->map == shim)
		return;
	*real = klass->map;
	klass->map = shim;
	patch_children(type, *real, shim);
	note("hooked %s::map", name);
}

static void install_map_shims(void)
{
	patch_map(type_menu_button, &real_menu_button_map, shim_menu_button_map,
		  "GtkMenuButton");
	patch_map(type_popover_menu_bar, &real_menu_bar_map, shim_menu_bar_map,
		  "GtkPopoverMenuBar");
}

static void waiter_add(struct record *rec, GtkWindow *window)
{
	if (waiter_find(window) != NULL)
		return;

	struct waiter *w = calloc(1, sizeof *w);
	if (w == NULL)
		return;

	w->window = window;
	p_g_object_add_weak_pointer((GObject *)window, (gpointer *)&w->window);
	w->app = p_g_object_ref(rec->app);
	w->rec = rec;
	w->on_unrealize = p_g_signal_connect_data(window, "unrealize",
						  G_CALLBACK(on_window_gone), w,
						  NULL, 0);
	w->on_destroy = p_g_signal_connect_data(window, "destroy",
						G_CALLBACK(on_window_gone), w,
						NULL, 0);
	w->next = waiters;
	waiters = w;
	waiting_count++;
	install_map_shims();
	note("the window waits for a menu (%d waiting)", waiting_count);
}

/* ---- attaching at realize ---------------------------------------------------- */

static void attach_menubar(GtkWindow *window)
{
	GtkApplication *app = p_gtk_window_get_application(window);

	if (app == NULL)
		return;

	struct record *rec = record_for(app);

	if (rec == NULL) {
		if (p_gtk_application_get_menubar(app) != NULL) {
			rec = record_new(app);
			if (rec != NULL)
				rec->state = TAKEN;
			note("the application already has a menubar");
			return;
		}
		/* gtk_application_set_menubar() requires both; checked first, and
		   in this order, so a failed check cannot raise a critical. */
		if (!p_g_application_get_is_registered((GApplication *)app)) {
			note("the application is not registered; nothing set");
			return;
		}
		if (p_g_application_get_is_remote((GApplication *)app)) {
			note("the application is remote; nothing set");
			return;
		}

		rec = record_new(app);
		if (rec == NULL)
			return;
		rec->container = p_g_menu_new();
		rec->inner = p_g_menu_new();
		p_g_object_set_data((GObject *)rec->inner, COPY_KEY,
				    GINT_TO_POINTER(1));
		p_gtk_application_set_menubar(app, (GMenuModel *)rec->container);
		if (p_gtk_application_get_menubar(app) !=
		    (GMenuModel *)rec->container) {
			rec->state = TAKEN;
			release_menus(rec);
			note("our menubar was not taken; nothing set");
			return;
		}
		rec->state = OURS_EMPTY;
		p_g_signal_connect_data(app, "notify::menubar",
					G_CALLBACK(on_menubar_notify), rec, NULL, 0);
		note("empty menubar set");
	} else if (rec->state != TAKEN &&
		   p_gtk_application_get_menubar(app) !=
			   (GMenuModel *)rec->container) {
		go_taken(rec);
	}

	switch (rec->state) {
	case TAKEN:
		note("the application has a menubar of its own");
		return;

	case OURS_FILLED: {
		/* The menubar is per application, but "win." resolves against
		   the focused window. A second window of the same application
		   needs the same stand-ins in its own map; the copy made for it
		   is thrown away. */
		struct look lk = {
			.late = 0,
			.ours = { (GMenuModel *)rec->container,
				  (GMenuModel *)rec->inner },
		};
		GtkWidget *owner = NULL;
		GMenuModel *model = find_menu_model(window, &owner, &lk);
		if (model != NULL) {
			struct scope scope = {
				.owner = owner,
				.map = is_a(window, type_app_window)
					       ? (GActionMap *)window
					       : NULL,
			};
			p_g_object_unref(clean_model(model, 0, &scope));
			note("menubar already ours, stand-ins added for this window");
		}
		return;
	}

	case OURS_EMPTY:
		if (look(window, rec, 0))
			return;
		if (is_a(window, type_app_window))
			waiter_add(rec, window);
		return;
	}
}

/* ---- the hook ------------------------------------------------------------ */

static void (*real_window_realize)(GtkWidget *);
static void (*real_app_window_realize)(GtkWidget *);

static void shim_window_realize(GtkWidget *widget)
{
	/* GtkApplicationWindow's realize chains up to this one; when it has a
	   hook of its own the window has already been handled there. */
	if (real_app_window_realize == NULL || !is_a(widget, type_app_window))
		attach_menubar((GtkWindow *)widget);
	if (real_window_realize != NULL)
		real_window_realize(widget);
}

static void shim_app_window_realize(GtkWidget *widget)
{
	attach_menubar((GtkWindow *)widget);
	if (real_app_window_realize != NULL)
		real_app_window_realize(widget);
}

static int wanted_here(void)
{
	if (getenv("UNITY_GTK4_MENU_FORCE") != NULL)
		return 1;

	const char *desktop = getenv("XDG_CURRENT_DESKTOP");
	if (desktop == NULL || strstr(desktop, "Unity") == NULL) {
		note("XDG_CURRENT_DESKTOP=%s is not Unity",
		     desktop ? desktop : "(unset)");
		return 0;
	}

	return 1;
}

/*
 * Where GTK4 comes from decides when we can hook it.
 *
 * A C application links libgtk-4.so.1, so GTK4 is mapped before any
 * constructor runs and shim_init() finds it at once.
 *
 * A gjs or PyGObject application does not link GTK at all. GObject
 * Introspection loads it later, when the script imports Gtk, and resolves
 * each function through g_module_symbol() before calling it. So the first
 * g_module_symbol() for a gtk_ or adw_ name is the moment GTK4 is mapped but
 * none of its classes has been initialised yet - the same state the
 * constructor sees in a C application. We intercept it for that alone, and
 * always hand the lookup on unchanged. gtk-nocsd, which Ubuntu Unity also
 * preloads, intercepts the same function for the same reason; with both
 * loaded, ours runs first and hands the lookup on to gtk-nocsd's.
 */
enum { UNDECIDED, NOT_WANTED, WAITING, HOOKING, DONE };
static int state = UNDECIDED;
static int ready; /* every symbol resolved */

static int wanted(void)
{
	if (state == UNDECIDED) {
		debug_on = getenv("UNITY_GTK4_MENU_DEBUG") != NULL;
		state = wanted_here() ? WAITING : NOT_WANTED;
	}
	return state != NOT_WANTED;
}

static void hook_class(GType type, void (**real)(GtkWidget *),
		       void (*shim)(GtkWidget *), const char *name)
{
	GtkWidgetClass *klass = (GtkWidgetClass *)p_g_type_class_ref(type);

	if (klass == NULL || klass->realize == shim_window_realize ||
	    klass->realize == shim_app_window_realize)
		return;
	*real = klass->realize;
	klass->realize = shim;
	note("hooked %s::realize", name);
}

/* Hook GTK4 if it is mapped. Returns 0 while it is not, so the caller may try
   again later; anything else is final. */
static int try_hook(const char *when)
{
	if (!__sync_bool_compare_and_swap(&state, WAITING, HOOKING))
		return 1;

	/*
	 * RTLD_NOLOAD is the whole point: it returns a handle only if the
	 * library is ALREADY mapped into this process. In anything that is not
	 * a GTK4 application - which is most of the processes on the machine -
	 * this returns NULL and we stop here without having touched GTK.
	 */
	void *gtk = dlopen("libgtk-4.so.1", RTLD_LAZY | RTLD_NOLOAD);
	if (gtk == NULL) {
		state = WAITING;
		return 0;
	}

	state = DONE;
	note("GTK4 found %s", when);

	void *gobj = dlopen("libgobject-2.0.so.0", RTLD_LAZY | RTLD_NOLOAD);
	void *glib = dlopen("libglib-2.0.so.0", RTLD_LAZY | RTLD_NOLOAD);
	void *gio = dlopen("libgio-2.0.so.0", RTLD_LAZY | RTLD_NOLOAD);

	if (gobj == NULL || glib == NULL || gio == NULL) {
		note("GTK4 is present but GLib is not, which should not happen");
		return 1;
	}

	if (!resolve_all(gtk, gobj, glib, gio)) {
		note("could not resolve every symbol, doing nothing");
		return 1;
	}

	ready = 1;
	hook_class(type_window, &real_window_realize, shim_window_realize,
		   "GtkWindow");
	hook_class(type_app_window, &real_app_window_realize,
		   shim_app_window_realize, "GtkApplicationWindow");
	return 1;
}

__attribute__((constructor)) static void shim_init(void)
{
	if (wanted() && !try_hook("at load"))
		note("GTK4 is not loaded in this process yet");
}

/*
 * The next definition of a function we wrap, in load order after ours.
 *
 * Not through the dlsym we link against: gtk-nocsd replaces dlsym. Its
 * replacement answers "g_module_symbol" with the address of its own
 * g_module_symbol, and when gtk-nocsd is built without -Bsymbolic-functions
 * (upstream's plain make; Ubuntu's build flags add it) that address is taken
 * through its GOT - so it is ours, and we would call ourselves until the
 * stack runs out, in either LD_PRELOAD order. And unless its dlsym ends in a
 * tail call (-O0 again), glibc counts RTLD_NEXT from gtk-nocsd rather than
 * from us, so with gtk-nocsd first the "next" gtk_widget_action_set_enabled
 * is ours again.
 *
 * glibc's own dlsym is versioned and gtk-nocsd's is not, so asking dlvsym
 * for dlsym@GLIBC_2.34 finds glibc's; called from here, its RTLD_NEXT counts
 * from us. (dlvsym straight for the wrapped name does not work: every
 * object that links glibc has a version table, and glibc then refuses an
 * unversioned symbol under any version asked.) The answer is still checked
 * against ourselves: the one that must never come back. Our issue #1; the
 * measurement is in unity-distro's docs/research/nocsd-order/.
 *
 * RTLD_NEXT only searches the global scope, and the library that owns the
 * function may not be in it: Python opens its extension modules without
 * RTLD_GLOBAL, so in a PyGObject process libgmodule is in _gi's local scope,
 * while _gi's calls still bind to us, preloaded. RTLD_NEXT then finds
 * nothing. Without gtk-nocsd after us to answer instead, every lookup failed
 * and PyGObject stopped at its first import. So when RTLD_NEXT finds nothing,
 * take the definition from its owner, OWNER, if that is already loaded -
 * RTLD_NOLOAD never loads it into a process that has none. A handle's
 * lookup searches the owner and its dependencies only, never the preloaded
 * libraries, so it cannot come back with ours or gtk-nocsd's. The handle is
 * kept: we keep calling the function. unity-distro UNITY-20261013-013.
 */
static void *next_symbol(const char *name, void *self, const char *owner)
{
	static void *(*libc_dlsym)(void *, const char *);

	if (libc_dlsym == NULL)
		*(void **)(&libc_dlsym) = dlvsym(RTLD_DEFAULT, "dlsym",
						 "GLIBC_2.34");
	if (libc_dlsym == NULL)
		*(void **)(&libc_dlsym) = dlvsym(RTLD_DEFAULT, "dlsym",
						 "GLIBC_2.2.5");

	void *next = libc_dlsym != NULL ? libc_dlsym(RTLD_NEXT, name)
					: dlsym(RTLD_NEXT, name);
	if (next == NULL) {
		void *lib = dlopen(owner, RTLD_LAZY | RTLD_NOLOAD);
		if (lib != NULL)
			next = libc_dlsym != NULL ? libc_dlsym(lib, name)
						  : dlsym(lib, name);
		if (next != NULL)
			note("%s is not in the global scope; taking %s's own",
			     name, owner);
	}
	if (next == self) {
		note("the next %s is our own; not calling it", name);
		return NULL;
	}
	return next;
}

/* Our own entry points, bound locally whatever the link flags. */
extern __typeof__(g_module_symbol) self_g_module_symbol
	__attribute__((alias("g_module_symbol"), visibility("hidden")));
extern __typeof__(gtk_widget_action_set_enabled) self_action_set_enabled
	__attribute__((alias("gtk_widget_action_set_enabled"),
		       visibility("hidden")));

gboolean g_module_symbol(GModule *module, const gchar *symbol_name,
			 gpointer *symbol)
{
	static gboolean (*real)(GModule *, const gchar *, gpointer *);
	static int missing;

	if (real == NULL)
		*(void **)(&real) = next_symbol("g_module_symbol",
						  (void *)self_g_module_symbol,
						  "libgmodule-2.0.so.0");
	if (real == NULL && !missing) {
		missing = 1;
		note("no g_module_symbol to pass lookups on to");
	}

	gboolean found = real != NULL ? real(module, symbol_name, symbol) : FALSE;

	/* After the lookup, not before: gtk-nocsd fills in its GTK and
	   libadwaita types in its own g_module_symbol, the first time it sees
	   GTK loaded there. Our hook initialises GtkWindow's class, which
	   registers types through its g_type_register_static_simple instead;
	   if that comes first, gtk-nocsd notices GTK there, never fetches the
	   types, and later takes an AdwApplicationWindow for a plain window -
	   gnome-characters aborted on exactly that. The symbol is only
	   resolved, not yet called, so no class is initialised by then. */
	if (state == WAITING && symbol_name != NULL &&
	    (strncmp(symbol_name, "gtk_", 4) == 0 ||
	     strncmp(symbol_name, "adw_", 4) == 0))
		try_hook("through GObject Introspection");

	/* An introspected application calls GTK through the pointer returned
	   here, never through the PLT, so hand it our setter directly. */
	if (found && ready && symbol != NULL &&
	    strcmp(symbol_name, "gtk_widget_action_set_enabled") == 0)
		*symbol = (gpointer)gtk_widget_action_set_enabled;

	return found;
}

/*
 * The only way to learn a class action's enabled state. A C or Rust
 * application reaches this through the PLT; a gjs or Python one through the
 * g_module_symbol() above. The real setter always runs first and unchanged.
 */
void gtk_widget_action_set_enabled(GtkWidget *widget, const char *action_name,
				   gboolean enabled)
{
	static void (*real)(GtkWidget *, const char *, gboolean);
	static int missing;

	if (real == NULL)
		*(void **)(&real) = next_symbol("gtk_widget_action_set_enabled",
						  (void *)self_action_set_enabled,
						  "libgtk-4.so.1");
	if (real == NULL && !missing) {
		missing = 1;
		note("no gtk_widget_action_set_enabled to call");
	}
	if (real != NULL)
		real(widget, action_name, enabled);

	if (!ready || widget == NULL || action_name == NULL)
		return;

	char key[256];
	make_key(key, sizeof key, ENABLED_KEY, action_name);
	p_g_object_set_data((GObject *)widget, key,
			    GINT_TO_POINTER(enabled ? 1 : 2));

	make_key(key, sizeof key, PROXY_KEY, action_name);
	GSimpleAction *proxy = p_g_object_get_data((GObject *)widget, key);
	if (proxy != NULL)
		p_g_simple_action_set_enabled(proxy, enabled);
}
