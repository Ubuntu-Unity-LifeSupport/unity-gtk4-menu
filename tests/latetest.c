/*
 * latetest - menus that appear after realize (unity-gtk4-menu 0.11).
 *
 * One case per process. Each case builds windows the way some application
 * does, runs the main loop briefly, and checks what the global menu would
 * see: the window's _GTK_MENUBAR_OBJECT_PATH, the application's menubar, and
 * the items a real D-Bus client (GDBusMenuModel, subscribed before the menu
 * appears) receives. With UNITY_GTK4_MENU_LOG set, it also checks the
 * library's own log lines; LATETEST_CONTROL=1 skips those, for a run against
 * a release that does not write them (the control run against 0.10).
 *
 * Not part of `make check`: it needs an X display, a D-Bus session and the
 * library preloaded. Run on a test machine, for example:
 *
 *   cc -o latetest tests/latetest.c $(pkg-config --cflags --libs gtk4 x11)
 *   for c in 1 2 3 4 5 7 8 9 10 11 12; do
 *     dbus-run-session -- xvfb-run -a env G_DEBUG=fatal-criticals \
 *       UNITY_GTK4_MENU_FORCE=1 UNITY_GTK4_MENU_DEBUG=1 \
 *       UNITY_GTK4_MENU_LOG=/tmp/latetest-$c.log \
 *       LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libunity-gtk4-menu.so.0 \
 *       ./latetest $c
 *   done
 *
 * Exit status 0 = PASS, 1 = FAIL (with a FAIL line on stderr).
 *
 * Cases:
 *   1  a menu button packed into a header bar after present (Pinta's order)
 *   2  a popover menu bar packed into a header bar after present
 *   3  a window that never gets a menu
 *   4  a menu button mapped without a menu, given one later
 *   5  the application sets its own menubar after realize, then NULL
 *   7  a plain window without a menu first, then an application window
 *      with a menu at realize (gnome-contacts' order)
 *   8  a menu present at realize (the 0.10 path)
 *   9  a GtkMenuButton subclass whose class was initialised before
 *  10  an application window realized before its application is registered
 *  11  a late menu with nothing that can cross D-Bus, then a valid one
 *  12  a waiting window destroyed after the last application reference
 *      outside it was dropped
 * (6 and 6a, which need libadwaita, are in latetest-adw.py.)
 */
#include <gtk/gtk.h>
#include <gdk/x11/gdkx.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int which;
static int control;
static int failed;
static GtkApplication *app;
static GMenuModel *client; /* a GDBusMenuModel of our own export */

#define CHECK(cond, ...)                                                        \
	do {                                                                    \
		if (!(cond)) {                                                  \
			fprintf(stderr, "FAIL case %d: ", which);               \
			fprintf(stderr, __VA_ARGS__);                           \
			fputc('\n', stderr);                                    \
			failed = 1;                                             \
		}                                                               \
	} while (0)

/* ---- helpers -------------------------------------------------------------- */

static void spin(int ms)
{
	gint64 end = g_get_monotonic_time() + (gint64)ms * 1000;
	while (g_get_monotonic_time() < end)
		g_main_context_iteration(NULL, FALSE);
}

/* The window's _GTK_MENUBAR_OBJECT_PATH, or NULL. */
static char *menubar_path(GtkWidget *window)
{
	GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(window));
	if (surface == NULL || !GDK_IS_X11_SURFACE(surface))
		return NULL;

	Display *dpy = GDK_DISPLAY_XDISPLAY(gdk_surface_get_display(surface));
	Window xid = gdk_x11_surface_get_xid(surface);
	Atom prop = XInternAtom(dpy, "_GTK_MENUBAR_OBJECT_PATH", False);
	Atom utf8 = XInternAtom(dpy, "UTF8_STRING", False);
	Atom type;
	int format;
	unsigned long n, after;
	unsigned char *data = NULL;
	char *out = NULL;

	if (XGetWindowProperty(dpy, xid, prop, 0, 1024, False, utf8, &type,
			       &format, &n, &after, &data) == Success &&
	    data != NULL) {
		out = g_strndup((char *)data, n);
		XFree(data);
	}
	return out;
}

/* Items under the holder of a menubar model: 0 when the menubar is empty. */
static int holder_items(GMenuModel *menubar)
{
	if (menubar == NULL || g_menu_model_get_n_items(menubar) == 0)
		return 0;
	GMenuModel *sub = g_menu_model_get_item_link(menubar, 0, "submenu");
	if (sub == NULL)
		return -1;
	int n = g_menu_model_get_n_items(sub);
	g_object_unref(sub);
	return n;
}

/* Subscribe to our own exported menubar as the panel would. */
static void subscribe(GtkWidget *window)
{
	char *path = menubar_path(window);
	if (path == NULL)
		return;
	GDBusConnection *bus =
		g_application_get_dbus_connection(G_APPLICATION(app));
	client = G_MENU_MODEL(g_dbus_menu_model_get(
		bus, g_dbus_connection_get_unique_name(bus), path));
	g_free(path);
	/* GDBusMenuModel subscribes when its item count is first asked for. */
	g_menu_model_get_n_items(client);
	spin(300);
}

/* What the D-Bus client sees under the holder, after waiting up to 3 s. */
static int client_items(void)
{
	if (client == NULL)
		return -2;
	for (int i = 0; i < 30; i++) {
		int n = holder_items(client);
		if (n > 0)
			return n;
		spin(100);
	}
	return holder_items(client);
}

static int log_has(const char *text)
{
	const char *path = g_getenv("UNITY_GTK4_MENU_LOG");
	char *data = NULL;
	int found = 0;

	if (path != NULL && g_file_get_contents(path, &data, NULL, NULL)) {
		found = strstr(data, text) != NULL;
		g_free(data);
	}
	return found;
}

#define CHECK_LOG(present, text)                                               \
	do {                                                                   \
		if (!control)                                                  \
			CHECK(log_has(text) == (present),                      \
			      "log line \"%s\" expected %s", text,             \
			      (present) ? "present" : "absent");               \
	} while (0)

/* A menu reaching app. actions, as Pinta's main menu does. */
static GMenuModel *main_menu(void)
{
	GMenu *file = g_menu_new();
	g_menu_append(file, "_New", "app.new");
	g_menu_append(file, "_Quit", "app.quit");
	GMenu *top = g_menu_new();
	g_menu_append_submenu(top, "_File", G_MENU_MODEL(file));
	g_object_unref(file);
	return G_MENU_MODEL(top);
}

static void noop(GSimpleAction *a, GVariant *p, gpointer d)
{
}

static GtkWidget *app_window_with_header(GtkWidget **header)
{
	GtkWidget *window = gtk_application_window_new(app);
	*header = gtk_header_bar_new();
	gtk_window_set_titlebar(GTK_WINDOW(window), *header);
	gtk_window_set_default_size(GTK_WINDOW(window), 300, 200);
	return window;
}

static GtkWidget *menu_button(GMenuModel *model)
{
	GtkWidget *button = gtk_menu_button_new();
	if (model != NULL)
		gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(button), model);
	return button;
}

/* ---- the cases ------------------------------------------------------------ */

/* A subclass of GtkMenuButton registered at run time, as GI and .NET
   applications do (Pinta's ToolBarDropDownButton). GtkMenuButton is not
   final but its instance struct is private, so the subclass is registered
   with the sizes GType reports. */
static GType sub_button_type(void)
{
	static GType type;
	if (type == 0) {
		GTypeQuery q;
		g_type_query(GTK_TYPE_MENU_BUTTON, &q);
		type = g_type_register_static_simple(GTK_TYPE_MENU_BUTTON,
						     "LateTestSubButton",
						     q.class_size, NULL,
						     q.instance_size, NULL, 0);
	}
	return type;
}

static void run_case(void)
{
	GtkWidget *header, *window;
	GMenuModel *menu = main_menu();
	char *path;

	switch (which) {
	case 1: /* Pinta's order */
	case 9: /* the same with a subclass initialised beforehand */
		if (which == 9)
			g_type_class_unref(g_type_class_ref(sub_button_type()));
		window = app_window_with_header(&header);
		gtk_window_present(GTK_WINDOW(window));
		spin(200);
		path = menubar_path(window);
		CHECK(control || path != NULL,
		      "no _GTK_MENUBAR_OBJECT_PATH on the window after realize");
		g_free(path);
		subscribe(window);
		{
			GtkWidget *b = which == 9 ? g_object_new(sub_button_type(), NULL)
						  : menu_button(NULL);
			gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(b), menu);
			gtk_header_bar_pack_end(GTK_HEADER_BAR(header), b);
		}
		spin(500);
		CHECK(holder_items(gtk_application_get_menubar(app)) > 0,
		      "the late menu is not in the application's menubar");
		CHECK(client_items() > 0,
		      "a subscribed D-Bus client does not see the late menu");
		CHECK_LOG(1, "empty menubar set");
		CHECK_LOG(1, "hooked GtkMenuButton::map");
		CHECK_LOG(1, "menubar filled late");
		break;

	case 2: /* a popover menu bar after present */
		window = app_window_with_header(&header);
		gtk_window_present(GTK_WINDOW(window));
		spin(200);
		subscribe(window);
		gtk_header_bar_pack_start(GTK_HEADER_BAR(header),
					  gtk_popover_menu_bar_new_from_model(menu));
		spin(500);
		CHECK(holder_items(gtk_application_get_menubar(app)) > 0,
		      "the late popover menu bar is not in the menubar");
		CHECK(client_items() > 0,
		      "a subscribed D-Bus client does not see the late menu bar");
		CHECK_LOG(1, "menubar filled late");
		break;

	case 3: /* never a menu */
		window = app_window_with_header(&header);
		gtk_window_present(GTK_WINDOW(window));
		spin(300);
		gtk_header_bar_pack_end(GTK_HEADER_BAR(header),
					gtk_button_new_with_label("plain"));
		spin(300);
		CHECK(holder_items(gtk_application_get_menubar(app)) == 0,
		      "a window without a menu exports items");
		CHECK_LOG(1, "the window waits for a menu");
		gtk_window_destroy(GTK_WINDOW(window));
		spin(200);
		CHECK_LOG(1, "a window stopped waiting (0 waiting)");
		break;

	case 4: /* a button mapped without a menu */
		window = app_window_with_header(&header);
		gtk_window_present(GTK_WINDOW(window));
		spin(200);
		{
			GtkWidget *b = menu_button(NULL);
			gtk_header_bar_pack_end(GTK_HEADER_BAR(header), b);
			spin(300);
			CHECK(holder_items(gtk_application_get_menubar(app)) == 0,
			      "items before the button has a menu");
			gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(b), menu);
		}
		spin(500);
		CHECK(holder_items(gtk_application_get_menubar(app)) > 0,
		      "the menu given later is not in the menubar");
		CHECK_LOG(1, "menubar filled late");
		break;

	case 5: { /* the application sets its own menubar */
		window = app_window_with_header(&header);
		gtk_window_present(GTK_WINDOW(window));
		spin(200);
		GMenuModel *own = main_menu();
		gtk_application_set_menubar(app, own);
		spin(200);
		CHECK(gtk_application_get_menubar(app) == own,
		      "the application's own menubar was not kept");
		CHECK_LOG(1, "ours is released");
		gtk_header_bar_pack_end(GTK_HEADER_BAR(header), menu_button(menu));
		spin(300);
		CHECK(gtk_application_get_menubar(app) == own,
		      "ours replaced the application's menubar");
		gtk_application_set_menubar(app, NULL);
		GtkWidget *second = gtk_application_window_new(app);
		gtk_window_present(GTK_WINDOW(second));
		spin(300);
		CHECK(gtk_application_get_menubar(app) == NULL,
		      "ours was set again after the application set NULL");
		g_object_unref(own);
		break;
	}

	case 7: { /* gnome-contacts' order */
		GtkWidget *plain = gtk_window_new();
		gtk_window_set_application(GTK_WINDOW(plain), app);
		gtk_window_present(GTK_WINDOW(plain));
		spin(200);
		window = app_window_with_header(&header);
		gtk_header_bar_pack_end(GTK_HEADER_BAR(header), menu_button(menu));
		gtk_window_present(GTK_WINDOW(window));
		spin(300);
		CHECK(holder_items(gtk_application_get_menubar(app)) > 0,
		      "the second window's menu is lost");
		break;
	}

	case 8: /* the realize path */
		window = app_window_with_header(&header);
		gtk_header_bar_pack_end(GTK_HEADER_BAR(header), menu_button(menu));
		gtk_window_present(GTK_WINDOW(window));
		spin(300);
		CHECK(holder_items(gtk_application_get_menubar(app)) > 0,
		      "the menu present at realize is not exported");
		CHECK_LOG(0, "hooked GtkMenuButton::map");
		CHECK_LOG(0, "the window waits for a menu");
		break;

	case 11: { /* nothing usable first, then a valid menu */
		window = app_window_with_header(&header);
		gtk_window_present(GTK_WINDOW(window));
		spin(200);
		GMenu *custom = g_menu_new();
		GMenuItem *item = g_menu_item_new("Custom", "app.new");
		g_menu_item_set_attribute(item, "custom", "s", "widget");
		g_menu_append_item(custom, item);
		g_object_unref(item);
		gtk_header_bar_pack_end(GTK_HEADER_BAR(header),
					menu_button(G_MENU_MODEL(custom)));
		spin(300);
		CHECK(holder_items(gtk_application_get_menubar(app)) == 0,
		      "an empty cleaned menu was used");
		gtk_header_bar_pack_start(GTK_HEADER_BAR(header), menu_button(menu));
		spin(500);
		CHECK(holder_items(gtk_application_get_menubar(app)) > 0,
		      "the valid menu after an empty one was not used");
		g_object_unref(custom);
		break;
	}

	case 12: { /* destroyed after the last outside reference went */
		GtkApplication *other = gtk_application_new(
			"org.unitydistro.LateTestOther", G_APPLICATION_NON_UNIQUE);
		g_application_register(G_APPLICATION(other), NULL, NULL);
		GtkWidget *w = gtk_application_window_new(other);
		gtk_window_present(GTK_WINDOW(w));
		spin(300);
		g_object_unref(other); /* the window keeps it alive */
		gtk_window_destroy(GTK_WINDOW(w));
		spin(300);
		CHECK_LOG(1, "a window stopped waiting");
		break;
	}
	}

	g_object_unref(menu);
	if (client != NULL)
		g_object_unref(client);
	g_application_quit(G_APPLICATION(app));
}

static void on_activate(GApplication *a, gpointer d)
{
	static const GActionEntry actions[] = {
		{ "new", noop }, { "quit", noop },
	};
	g_action_map_add_action_entries(G_ACTION_MAP(a), actions,
					G_N_ELEMENTS(actions), NULL);
	g_application_hold(a);
	run_case();
	g_application_release(a);
}

int main(int argc, char **argv)
{
	which = argc > 1 ? atoi(argv[1]) : 1;
	control = g_getenv("LATETEST_CONTROL") != NULL;

	if (which == 10) {
		/* realized before the application is registered */
		gtk_init();
		app = gtk_application_new("org.unitydistro.LateTest10",
					  G_APPLICATION_NON_UNIQUE);
		GtkWidget *w = gtk_application_window_new(app);
		gtk_widget_realize(w);
		CHECK_LOG(1, "the application is not registered");
		gtk_window_destroy(GTK_WINDOW(w));
		g_object_unref(app);
		printf("%s case %d\n", failed ? "FAIL" : "PASS", which);
		return failed;
	}

	char *id = g_strdup_printf("org.unitydistro.LateTest%d", which);
	app = gtk_application_new(id, G_APPLICATION_NON_UNIQUE);
	g_free(id);
	g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
	g_application_run(G_APPLICATION(app), 1, argv);
	g_object_unref(app);

	printf("%s case %d%s\n", failed ? "FAIL" : "PASS", which,
	       control ? " (control: library log not checked)" : "");
	return failed;
}
