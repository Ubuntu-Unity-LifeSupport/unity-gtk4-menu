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
 * nothing unless GTK4 is already loaded in this process. The headers are
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
 * Only class actions positively found on the owner or its ancestors get one;
 * a property action (gtk_widget_class_install_property_action) carries state
 * a plain stand-in cannot mirror, so it is left alone and logged. So are names
 * with a prefix other than app. or win., which usually come from a group
 * inserted on a sub-widget and cannot be enumerated through public API.
 *
 * The stand-in is always enabled: GTK has no public getter for a class
 * action's enabled state. Activating a disabled one does nothing, as in the
 * application.
 */
#define PROXY_PREFIX "unity-gtk4-menu-"

struct proxy {
	GtkWidget *owner; /* weak */
	gchar *name;
};

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
	p_g_free(proxy->name);
	free(proxy);
}

/* Returns 1 for a stateless class action on @widget or an ancestor. */
static int find_class_action(GtkWidget *widget, const char *name,
			     const GVariantType **parameter_type)
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
			if (property_name != NULL) {
				note("%s is a property action, not proxied",
				     name);
				return 0;
			}
			*parameter_type = ptype;
			return 1;
		}
	}
	return 0;
}

/* The name the exported item should use for @name: a stand-in in @map when
   @name is a class action reachable from @owner, otherwise NULL - keep it. */
static gchar *proxy_for(const char *name, GtkWidget *owner, GActionMap *map)
{
	const GVariantType *ptype = NULL;

	if (!find_class_action(owner, name, &ptype)) {
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
		proxy->name = p_g_strdup(name);
		p_g_object_add_weak_pointer((GObject *)owner,
					    (gpointer *)&proxy->owner);

		GSimpleAction *action = p_g_simple_action_new(local, ptype);
		p_g_signal_connect_data(action, "activate",
					(GCallback)proxy_activate, proxy,
					proxy_free, 0);
		p_g_action_map_add_action(map, (GAction *)action);
		p_g_object_unref(action);
		note("%s proxied as win.%s", name, local);
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

__attribute__((constructor)) static void shim_init(void)
{
	debug_on = getenv("UNITY_GTK4_MENU_DEBUG") != NULL;

	if (!wanted_here())
		return;

	/*
	 * RTLD_NOLOAD is the whole point: it returns a handle only if the
	 * library is ALREADY mapped into this process. In anything that is not
	 * a GTK4 application - which is most of the processes on the machine -
	 * this returns NULL and we stop here without having touched GTK.
	 */
	void *gtk = dlopen("libgtk-4.so.1", RTLD_LAZY | RTLD_NOLOAD);
	if (gtk == NULL) {
		note("GTK4 is not loaded in this process, doing nothing");
		return;
	}

	void *gobj = dlopen("libgobject-2.0.so.0", RTLD_LAZY | RTLD_NOLOAD);
	void *glib = dlopen("libglib-2.0.so.0", RTLD_LAZY | RTLD_NOLOAD);
	void *gio = dlopen("libgio-2.0.so.0", RTLD_LAZY | RTLD_NOLOAD);

	if (gobj == NULL || glib == NULL || gio == NULL) {
		note("GTK4 is present but GLib is not, which should not happen");
		return;
	}

	if (!resolve_all(gtk, gobj, glib, gio)) {
		note("could not resolve every symbol, doing nothing");
		return;
	}

	GtkWidgetClass *window_class =
		(GtkWidgetClass *)p_g_type_class_ref(type_window);
	if (window_class != NULL) {
		real_window_realize = window_class->realize;
		window_class->realize = shim_window_realize;
		note("hooked GtkWindow::realize");
	}

	GtkWidgetClass *app_window_class =
		(GtkWidgetClass *)p_g_type_class_ref(type_app_window);
	if (app_window_class != NULL &&
	    app_window_class->realize != shim_window_realize) {
		real_app_window_realize = app_window_class->realize;
		app_window_class->realize = shim_app_window_realize;
		note("hooked GtkApplicationWindow::realize");
	}
}
