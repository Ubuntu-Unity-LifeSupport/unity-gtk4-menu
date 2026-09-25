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

static GType type_window, type_app_window, type_menu_button;
static GType type_popover_menu, type_popover_menu_bar;

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

	type_window = p_gtk_window_get_type();
	type_app_window = p_gtk_application_window_get_type();
	type_menu_button = p_gtk_menu_button_get_type();
	type_popover_menu = p_gtk_popover_menu_get_type();
	type_popover_menu_bar = p_gtk_popover_menu_bar_get_type();

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

static void collect_menus(GtkWidget *widget, int depth, struct candidates *out)
{
	if (widget == NULL || depth > 32)
		return;

	if (is_a(widget, type_menu_button)) {
		GtkMenuButton *button = (GtkMenuButton *)widget;
		GMenuModel *model = p_gtk_menu_button_get_menu_model(button);

		/* An application may call gtk_menu_button_set_popover() rather
		   than set_menu_model(), leaving get_menu_model() NULL though a
		   menu exists - yelp does this for all four of its buttons.
		   Where the popover is a GtkPopoverMenu the model is still
		   reachable. */
		if (model == NULL) {
			GtkPopover *popover = p_gtk_menu_button_get_popover(button);
			if (is_a(popover, type_popover_menu))
				model = p_gtk_popover_menu_get_menu_model(
					(GtkPopoverMenu *)popover);
		}

		if (model != NULL) {
			int primary = p_gtk_menu_button_get_primary != NULL &&
				      p_gtk_menu_button_get_primary(button);
			add_candidate(out, widget, model, primary, depth);
		}
	}

	if (is_a(widget, type_popover_menu_bar)) {
		GMenuModel *model = p_gtk_popover_menu_bar_get_menu_model(
			(GtkPopoverMenuBar *)widget);
		if (model != NULL)
			add_candidate(out, widget, model, 0, depth);
	}

	for (GtkWidget *child = p_gtk_widget_get_first_child(widget);
	     child != NULL; child = p_gtk_widget_get_next_sibling(child))
		collect_menus(child, depth + 1, out);
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
static GMenuModel *find_menu_model(GtkWindow *window, GtkWidget **owner)
{
	struct candidates found = { .n = 0 };

	/* The title bar is not part of the ordinary child tree, and yelp keeps
	   its header bar in the content rather than the title bar. Try both. */
	collect_menus(p_gtk_window_get_titlebar(window), 0, &found);
	collect_menus(p_gtk_window_get_child(window), 0, &found);

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
 * separators remain. The copy is static: it does not follow later changes to
 * the original, which is acceptable for header bar menus, which are built once.
 */
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

	for (gint i = 0; i < n; i++) {
		if (has_attribute(model, i, "custom")) {
			scope->dropped++;
			continue;
		}

		GMenuItem *item = p_g_menu_item_new_from_model(model, i);
		int keep = 1;

		proxy_item(model, i, item, scope);

		const char *links[] = { "section", "submenu" };
		for (int l = 0; l < 2 && depth < 16; l++) {
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

/* The menubar we set, to tell it from one the application set itself. */
static GMenuModel *our_menubar;

static void attach_menubar(GtkWindow *window)
{
	GtkApplication *app = p_gtk_window_get_application(window);

	if (app == NULL)
		return;

	GMenuModel *existing = p_gtk_application_get_menubar(app);
	if (existing != NULL && existing != our_menubar) {
		note("the application already has a menubar");
		return;
	}

	GtkWidget *owner = NULL;
	GMenuModel *model = find_menu_model(window, &owner);

	if (model == NULL) {
		note("no menu model in this window");
		return;
	}

	struct scope scope = {
		.owner = owner,
		.map = is_a(window, type_app_window) ? (GActionMap *)window
						     : NULL,
	};
	GMenuModel *cleaned = clean_model(model, 0, &scope);

	/* The menubar is per application, but "win." resolves against the
	   focused window. A second window of the same application needs the
	   same stand-ins in its own map; the copy made for it is thrown away. */
	if (existing != NULL) {
		note("menubar already ours, stand-ins added for this window");
		p_g_object_unref(cleaned);
		return;
	}

	if (scope.dropped > 0)
		note("dropped %d item(s) that cannot cross D-Bus", scope.dropped);

	gchar *label = menu_label(app);
	GMenu *menubar = p_g_menu_new();
	p_g_menu_append_submenu(menubar, label, cleaned);
	p_g_object_unref(cleaned);
	p_gtk_application_set_menubar(app, (GMenuModel *)menubar);
	our_menubar = (GMenuModel *)menubar;
	p_g_object_unref(menubar);
	note("menubar attached, labelled \"%s\"", label);
	p_g_free(label);
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
 */
static void *next_symbol(const char *name, void *self)
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

	if (real == NULL)
		*(void **)(&real) = next_symbol("g_module_symbol",
						  (void *)self_g_module_symbol);

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

	if (real == NULL)
		*(void **)(&real) = next_symbol("gtk_widget_action_set_enabled",
						  (void *)self_action_set_enabled);
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
