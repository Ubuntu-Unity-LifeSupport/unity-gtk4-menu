/*
 * unity-gtk4-shim - experiment for Layer B of unity-distro.
 *
 * GTK4 has no module loading mechanism, so the way appmenu-gtk-module reaches
 * GTK3 applications is unavailable. What is available is symbol interposition,
 * the same technique libgtk-nocsd already uses in the Unity session.
 *
 * GTK4 still exports a menubar over org.gtk.Menus and advertises it through
 * _GTK_MENUBAR_OBJECT_PATH, and the Unity panel renders it. Applications
 * simply never call gtk_application_set_menubar(): their menu lives in a
 * GtkMenuButton in the header bar.
 *
 * The menubar has to be set before the window is realized - attaching one
 * afterwards silently does nothing. gtk_window_present() is the last moment
 * the application controls before realize, so that is where we intervene.
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <glib/gi18n.h>
#include <unistd.h>
#include <gtk/gtk.h>
#include <gdk/x11/gdkx.h>
#include <gio/gdesktopappinfo.h>

static int verbose(void) {
	static int v = -1;
	if (v < 0)
		v = getenv("UNITY_GTK4_SHIM_DEBUG") != NULL;
	return v;
}

/*
 * Log to a file rather than stderr when UNITY_GTK4_SHIM_LOG is set. The window
 * is often created in a different process from the one launched - file-roller
 * and simple-scan both do this - and that process inherits the environment but
 * not the caller's redirected stderr, so stderr output simply disappears.
 */
static void note_out(const char *fmt, ...)
{
	if (!verbose())
		return;

	va_list ap;
	const char *path = getenv("UNITY_GTK4_SHIM_LOG");
	FILE *out = stderr;

	if (path != NULL) {
		FILE *f = fopen(path, "a");
		if (f != NULL)
			out = f;
	}

	fprintf(out, "[unity-gtk4-shim %d] ", (int)getpid());
	va_start(ap, fmt);
	vfprintf(out, fmt, ap);
	va_end(ap);
	fputc('\n', out);
	fflush(out);

	if (out != stderr)
		fclose(out);
}

#define note(...) note_out(__VA_ARGS__)

/* Depth-first search for the first GtkMenuButton carrying a menu model. */
static GMenuModel *find_menu_model(GtkWidget *widget, int depth)
{
	if (widget == NULL || depth > 32)
		return NULL;

	if (GTK_IS_MENU_BUTTON(widget)) {
		GtkMenuButton *button = GTK_MENU_BUTTON(widget);
		GMenuModel *model = gtk_menu_button_get_menu_model(button);

		if (model != NULL) {
			note("found GtkMenuButton with a model at depth %d", depth);
			return model;
		}

		/*
		 * An application that calls gtk_menu_button_set_popover()
		 * instead of set_menu_model() leaves get_menu_model() NULL.
		 * yelp does this for all four of its menu buttons. When the
		 * popover is a GtkPopoverMenu the model is still reachable.
		 */
		GtkPopover *popover = gtk_menu_button_get_popover(button);
		if (popover != NULL && GTK_IS_POPOVER_MENU(popover)) {
			model = gtk_popover_menu_get_menu_model(
				GTK_POPOVER_MENU(popover));
			if (model != NULL) {
				note("found GtkPopoverMenu behind a menu button at depth %d",
				     depth);
				return model;
			}
		}
	}

	if (GTK_IS_POPOVER_MENU_BAR(widget)) {
		GMenuModel *model = gtk_popover_menu_bar_get_menu_model(
			GTK_POPOVER_MENU_BAR(widget));
		if (model != NULL) {
			note("found GtkPopoverMenuBar with a model at depth %d", depth);
			return model;
		}
	}

	for (GtkWidget *child = gtk_widget_get_first_child(widget);
	     child != NULL; child = gtk_widget_get_next_sibling(child)) {
		GMenuModel *model = find_menu_model(child, depth + 1);
		if (model != NULL)
			return model;
	}

	return NULL;
}

/* Print the top level of a model: what a hamburger menu is actually made of. */
static void dump_model(GMenuModel *model)
{
	if (!verbose() || model == NULL)
		return;

	int n = g_menu_model_get_n_items(model);
	note("model has %d top-level item(s)", n);

	for (int i = 0; i < n; i++) {
		char *label = NULL;
		gboolean has_label = g_menu_model_get_item_attribute(
			model, i, G_MENU_ATTRIBUTE_LABEL, "s", &label);
		GMenuModel *section =
			g_menu_model_get_item_link(model, i, G_MENU_LINK_SECTION);
		GMenuModel *submenu =
			g_menu_model_get_item_link(model, i, G_MENU_LINK_SUBMENU);

		note("  [%d] %s%s%s  label=%s", i,
		     section ? "section" : "", submenu ? "submenu" : "",
		     (!section && !submenu) ? "item" : "",
		     has_label ? label : "(none)");

		if (section != NULL) {
			note("       section holds %d item(s)",
			     g_menu_model_get_n_items(section));
			g_object_unref(section);
		}
		if (submenu != NULL)
			g_object_unref(submenu);
		if (has_label)
			g_free(label);
	}
}

/* Walk the widget tree printing types, to see what an application is made of. */
static void dump_tree(GtkWidget *widget, int depth)
{
	if (widget == NULL || depth > 24)
		return;

	const char *type = G_OBJECT_TYPE_NAME(widget);
	gboolean interesting = GTK_IS_MENU_BUTTON(widget) ||
			       GTK_IS_POPOVER_MENU_BAR(widget) ||
			       GTK_IS_POPOVER(widget);

	note("  tree %*s%s%s", depth * 2, "", type,
	     interesting ? "   <-- menu-ish" : "");

	if (GTK_IS_MENU_BUTTON(widget)) {
		GMenuModel *m = gtk_menu_button_get_menu_model(GTK_MENU_BUTTON(widget));
		GtkWidget *pop = GTK_WIDGET(gtk_menu_button_get_popover(GTK_MENU_BUTTON(widget)));
		note("  tree %*s     model=%s popover=%s", depth * 2, "",
		     m ? "yes" : "no", pop ? G_OBJECT_TYPE_NAME(pop) : "no");
	}

	for (GtkWidget *c = gtk_widget_get_first_child(widget); c != NULL;
	     c = gtk_widget_get_next_sibling(c))
		dump_tree(c, depth + 1);
}

#define SHIM_SCHEMA "com.ubuntu-unity.gtk4-menu"

/*
 * A hamburger menu has no name of its own, so the entry we create needs one.
 * Named after the application it repeats the name the panel already shows
 * beside it; named neutrally it does not, at the cost of saying less.
 *
 * There is no right answer, and the desktops that ship a global menu treat it
 * as a preference rather than settle it: Cinnamon's Global Application Menu
 * applet offers showing or hiding the application name, and vala-panel-appmenu
 * carries the same discussion. So it is a setting here too.
 *
 * UNITY_GTK4_SHIM_LABEL=app|generic overrides it for testing, and is also what
 * runs if the schema is not installed.
 */
static char *menu_label(GtkApplication *app)
{
	gboolean use_app_name = TRUE;
	const char *override = getenv("UNITY_GTK4_SHIM_LABEL");

	if (override != NULL) {
		use_app_name = (strcmp(override, "generic") != 0);
		note("label mode from the environment: %s", override);
	} else {
		GSettingsSchemaSource *source = g_settings_schema_source_get_default();
		GSettingsSchema *schema =
			source ? g_settings_schema_source_lookup(source, SHIM_SCHEMA, TRUE)
			       : NULL;
		if (schema != NULL) {
			GSettings *settings = g_settings_new(SHIM_SCHEMA);
			use_app_name = g_settings_get_boolean(
				settings, "show-application-name");
			g_object_unref(settings);
			g_settings_schema_unref(schema);
			note("show-application-name = %s",
			     use_app_name ? "true" : "false");
		} else {
			note("schema %s not installed, defaulting to the application name",
			     SHIM_SCHEMA);
		}
	}

	if (!use_app_name)
		return g_strdup(_("Menu"));

	/*
	 * Prefer the .desktop Name, which is the field Unity's panel shows, so
	 * that turning the setting on really does repeat what is beside it
	 * rather than something approximately like it.
	 */
	const char *app_id = g_application_get_application_id(G_APPLICATION(app));
	if (app_id != NULL) {
		char *desktop_id = g_strconcat(app_id, ".desktop", NULL);
		GDesktopAppInfo *info = g_desktop_app_info_new(desktop_id);
		g_free(desktop_id);

		if (info != NULL) {
			const char *name =
				g_app_info_get_name(G_APP_INFO(info));
			if (name != NULL) {
				char *result = g_strdup(name);
				g_object_unref(info);
				note("label from the .desktop file: %s", result);
				return result;
			}
			g_object_unref(info);
		}
	}

	const char *fallback = g_get_application_name();
	note("no .desktop match, falling back to %s",
	     fallback ? fallback : "Menu");
	return g_strdup(fallback ? fallback : "Menu");
}

/* Titlebar first, then the content tree - yelp keeps its header bar in the
   content, not in the titlebar. */
static GMenuModel *find_window_menu_model(GtkWindow *window)
{
	GMenuModel *model = find_menu_model(gtk_window_get_titlebar(window), 0);
	if (model == NULL)
		model = find_menu_model(gtk_window_get_child(window), 0);
	return model;
}

static void attach_menubar(GtkWindow *window)
{
	GtkApplication *app = gtk_window_get_application(window);

	if (app == NULL) {
		note("window has no GtkApplication, nothing to attach to");
		return;
	}
	if (gtk_application_get_menubar(app) != NULL) {
		note("application already has a menubar, leaving it alone");
		return;
	}

	if (getenv("UNITY_GTK4_SHIM_TREE") != NULL) {
		note("TREE titlebar:");
		dump_tree(gtk_window_get_titlebar(window), 0);
		note("TREE content:");
		dump_tree(gtk_window_get_child(window), 0);
	}

	/* The title bar is not part of the ordinary child tree. */
	GMenuModel *model = find_menu_model(gtk_window_get_titlebar(window), 0);
	if (model == NULL)
		model = find_menu_model(gtk_window_get_child(window), 0);

	if (model == NULL) {
		note("no menu model found in this window");
		if (getenv("UNITY_GTK4_SHIM_TREE") != NULL) {
			note("titlebar tree:");
			dump_tree(gtk_window_get_titlebar(window), 0);
			note("content tree:");
			dump_tree(gtk_window_get_child(window), 0);
		}
		return;
	}

	dump_model(model);

	char *app_label = menu_label(app);

	if (getenv("UNITY_GTK4_SHIM_DIRECT") != NULL) {
		/* Hand the model over as the menubar with no wrapper at all. */
		gtk_application_set_menubar(app, model);
		note("menubar attached directly, no wrapper");
		return;
	}

	GMenu *menubar = g_menu_new();

	if (getenv("UNITY_GTK4_SHIM_FLATTEN") != NULL) {
		/*
		 * Promote each section of the hamburger menu to its own
		 * top-level menu, which is the shape Unity expects from a
		 * GTK3 application. Sections mostly carry no label, so this
		 * only works as far as the labels do.
		 */
		int n = g_menu_model_get_n_items(model);
		int promoted = 0;

		for (int i = 0; i < n; i++) {
			GMenuModel *section = g_menu_model_get_item_link(
				model, i, G_MENU_LINK_SECTION);
			if (section == NULL)
				continue;

			char *label = NULL;
			if (!g_menu_model_get_item_attribute(
				    model, i, G_MENU_ATTRIBUTE_LABEL, "s",
				    &label))
				label = NULL;

			g_menu_append_submenu(menubar,
					      label ? label : app_label,
					      section);
			promoted++;

			g_free(label);
			g_object_unref(section);
		}

		note("flattened: promoted %d section(s) of %d", promoted, n);

		if (promoted == 0) {
			g_menu_append_submenu(menubar, app_label, model);
			note("nothing to promote, fell back to a single menu");
		}
	} else {
		/* One top-level entry holding the whole hamburger menu. */
		g_menu_append_submenu(menubar, app_label, model);
		note("menubar attached, labelled \"%s\"", app_label);
	}

	gtk_application_set_menubar(app, G_MENU_MODEL(menubar));
	g_object_unref(menubar);
	g_free(app_label);
}

/*
 * Interposing gtk_window_present / gtk_widget_set_visible / gtk_widget_show
 * catches applications that show their window themselves, but not ones where
 * the call is made from inside GTK or libadwaita - file-roller is one, and
 * none of the three ever fired for it.
 *
 * So do what appmenu-gtk-module does for GTK3: overwrite realize in the class
 * vtable. The reason that module cannot do it under GTK4 is that it has no way
 * to get loaded, which is exactly what LD_PRELOAD solves. Type registration
 * does not need gtk_init(), so g_type_class_ref works from a constructor.
 */

static void (*real_window_realize)(GtkWidget *) = NULL;
static void (*real_app_window_realize)(GtkWidget *) = NULL;

/*
 * Unity's indicator-appmenu reads five window properties, not one. Among them
 * _GTK_APP_MENU_OBJECT_PATH, the old GNOME application menu - and
 * add_application_menu() in window-menu-model.c labels that entry with the
 * application name on its own, falling back to "Unknown Application Name".
 *
 * GTK4 removed gtk_application_set_app_menu(), so no GTK4 application ever
 * sets that property, but the consumer still honours it. Export the model
 * ourselves and set the property, and Unity names the entry by its own
 * convention instead of one we invent.
 *
 * Must run after realize: before it there is no surface and no X11 window.
 */
static void publish_app_menu(GtkWindow *window, GMenuModel *model)
{
	GtkApplication *app = gtk_window_get_application(window);
	if (app == NULL || model == NULL)
		return;

	GDBusConnection *bus =
		g_application_get_dbus_connection(G_APPLICATION(app));
	const char *base =
		g_application_get_dbus_object_path(G_APPLICATION(app));
	if (bus == NULL || base == NULL) {
		note("no session bus or object path, cannot publish app menu");
		return;
	}

	char *path = g_strconcat(base, "/unityshim/appmenu", NULL);

	GError *error = NULL;
	guint id = g_dbus_connection_export_menu_model(bus, path, model, &error);
	if (id == 0) {
		note("export failed: %s", error ? error->message : "unknown");
		g_clear_error(&error);
		g_free(path);
		return;
	}

	GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(window));
	if (surface == NULL || !GDK_IS_X11_SURFACE(surface)) {
		note("not an X11 surface, cannot set the property");
		g_free(path);
		return;
	}

	Display *xdisplay = GDK_SURFACE_XDISPLAY(surface);
	Window xid = gdk_x11_surface_get_xid(GDK_X11_SURFACE(surface));

	XChangeProperty(xdisplay, xid,
			XInternAtom(xdisplay, "_GTK_APP_MENU_OBJECT_PATH", False),
			XInternAtom(xdisplay, "UTF8_STRING", False),
			8, PropModeReplace,
			(const unsigned char *)path, strlen(path));

	note("published app menu at %s", path);
	g_free(path);
}

static void shim_window_realize(GtkWidget *widget)
{
	note("realize (GtkWindow)");

	if (getenv("UNITY_GTK4_SHIM_APPMENU") != NULL) {
		if (real_window_realize != NULL)
			real_window_realize(widget);
		publish_app_menu(GTK_WINDOW(widget),
				 find_window_menu_model(GTK_WINDOW(widget)));
		return;
	}

	attach_menubar(GTK_WINDOW(widget));
	if (real_window_realize != NULL)
		real_window_realize(widget);
}

static void shim_app_window_realize(GtkWidget *widget)
{
	note("realize (GtkApplicationWindow)");

	if (getenv("UNITY_GTK4_SHIM_APPMENU") != NULL) {
		if (real_app_window_realize != NULL)
			real_app_window_realize(widget);
		publish_app_menu(GTK_WINDOW(widget),
				 find_window_menu_model(GTK_WINDOW(widget)));
		return;
	}

	attach_menubar(GTK_WINDOW(widget));
	if (real_app_window_realize != NULL)
		real_app_window_realize(widget);
}

/*
 * Only act under Unity. gtk-nocsd does the same thing in reverse - it disables
 * itself on everything GNOME except Flashback - because a library preloaded
 * session-wide ends up inside every process on the machine, including ones
 * that have nothing to do with the desktop it was meant for.
 *
 * gtk-nocsd goes further and removes itself: it blanks LD_PRELOAD in environ
 * and execve()s the program again. That is worth knowing but not worth copying
 * yet - a re-exec is a heavy thing to do inside somebody else's process, and
 * returning early costs nothing.
 */
static int wanted_here(void)
{
	const char *desktop = getenv("XDG_CURRENT_DESKTOP");

	if (getenv("UNITY_GTK4_SHIM_FORCE") != NULL)
		return 1;

	if (desktop == NULL || strstr(desktop, "Unity") == NULL) {
		note("XDG_CURRENT_DESKTOP=%s is not Unity, doing nothing",
		     desktop ? desktop : "(unset)");
		return 0;
	}

	return 1;
}

__attribute__((constructor)) static void shim_init(void)
{
	if (!wanted_here())
		return;

	note("env: DIRECT=%s FLATTEN=%s LOG=%s",
	     getenv("UNITY_GTK4_SHIM_DIRECT") ? "set" : "-",
	     getenv("UNITY_GTK4_SHIM_FLATTEN") ? "set" : "-",
	     getenv("UNITY_GTK4_SHIM_LOG") ? getenv("UNITY_GTK4_SHIM_LOG") : "-");

	GtkWidgetClass *window_class = g_type_class_ref(GTK_TYPE_WINDOW);
	if (window_class != NULL) {
		real_window_realize = window_class->realize;
		window_class->realize = shim_window_realize;
		note("hooked GtkWindow::realize");
	}

	GtkWidgetClass *app_window_class =
		g_type_class_ref(GTK_TYPE_APPLICATION_WINDOW);
	if (app_window_class != NULL &&
	    app_window_class->realize != real_window_realize) {
		real_app_window_realize = app_window_class->realize;
		app_window_class->realize = shim_app_window_realize;
		note("hooked GtkApplicationWindow::realize");
	}
}
