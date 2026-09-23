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
	RESOLVE(gio, g_menu_append_submenu);
	RESOLVE(gio, g_application_get_application_id);
	RESOLVE(gio, g_desktop_app_info_new);
	RESOLVE(gio, g_app_info_get_name);
	RESOLVE(gio, g_settings_schema_source_get_default);
	RESOLVE(gio, g_settings_schema_source_lookup);
	RESOLVE(gio, g_settings_schema_unref);
	RESOLVE(gio, g_settings_new);
	RESOLVE(gio, g_settings_get_boolean);

	RESOLVE(glib, g_get_application_name);
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

static GMenuModel *find_menu_model(GtkWidget *widget, int depth)
{
	if (widget == NULL || depth > 32)
		return NULL;

	if (is_a(widget, type_menu_button)) {
		GtkMenuButton *button = (GtkMenuButton *)widget;
		GMenuModel *model = p_gtk_menu_button_get_menu_model(button);

		if (model != NULL) {
			note("menu button with a model at depth %d", depth);
			return model;
		}

		/* An application may call gtk_menu_button_set_popover() rather
		   than set_menu_model(), leaving get_menu_model() NULL though a
		   menu exists - yelp does this for all four of its buttons.
		   Where the popover is a GtkPopoverMenu the model is still
		   reachable. */
		GtkPopover *popover = p_gtk_menu_button_get_popover(button);
		if (is_a(popover, type_popover_menu)) {
			model = p_gtk_popover_menu_get_menu_model(
				(GtkPopoverMenu *)popover);
			if (model != NULL) {
				note("popover menu behind a button at depth %d",
				     depth);
				return model;
			}
		}
	}

	if (is_a(widget, type_popover_menu_bar)) {
		GMenuModel *model = p_gtk_popover_menu_bar_get_menu_model(
			(GtkPopoverMenuBar *)widget);
		if (model != NULL) {
			note("popover menu bar at depth %d", depth);
			return model;
		}
	}

	for (GtkWidget *child = p_gtk_widget_get_first_child(widget);
	     child != NULL; child = p_gtk_widget_get_next_sibling(child)) {
		GMenuModel *model = find_menu_model(child, depth + 1);
		if (model != NULL)
			return model;
	}

	return NULL;
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

/* ---- attaching ----------------------------------------------------------- */

static void attach_menubar(GtkWindow *window)
{
	GtkApplication *app = p_gtk_window_get_application(window);

	if (app == NULL)
		return;
	if (p_gtk_application_get_menubar(app) != NULL) {
		note("the application already has a menubar");
		return;
	}

	/* The title bar is not part of the ordinary child tree, and yelp keeps
	   its header bar in the content rather than the title bar. Try both. */
	GMenuModel *model = find_menu_model(p_gtk_window_get_titlebar(window), 0);
	if (model == NULL)
		model = find_menu_model(p_gtk_window_get_child(window), 0);

	if (model == NULL) {
		note("no menu model in this window");
		return;
	}

	gchar *label = menu_label(app);
	GMenu *menubar = p_g_menu_new();
	p_g_menu_append_submenu(menubar, label, model);
	p_gtk_application_set_menubar(app, (GMenuModel *)menubar);
	p_g_object_unref(menubar);
	note("menubar attached, labelled \"%s\"", label);
	p_g_free(label);
}

/* ---- the hook ------------------------------------------------------------ */

static void (*real_window_realize)(GtkWidget *);
static void (*real_app_window_realize)(GtkWidget *);

static void shim_window_realize(GtkWidget *widget)
{
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
