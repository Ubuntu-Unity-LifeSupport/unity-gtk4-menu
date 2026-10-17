/*
 * latetest - menus that appear after realize (unity-gtk4-menu 0.12) and
 * changes of the menu after it is exported (0.13).
 *
 * One case per process. Each case builds windows the way some application
 * does, runs the main loop briefly, and checks what the global menu would
 * see: the window's _GTK_MENUBAR_OBJECT_PATH, the application's menubar, and
 * the items a real D-Bus client (GDBusMenuModel, subscribed before the menu
 * appears) receives. With UNITY_GTK4_MENU_LOG set, it also checks the
 * library's own log lines; LATETEST_CONTROL=1 skips those, for a run against
 * a release that does not write them (the control run against 0.11).
 * gtk-shell-shows-menubar is TRUE, as under Unity; LATETEST_SHELL_MENUBAR=0
 * sets it FALSE (run for cases 1 and 3).
 *
 * Not part of `make check`: it needs an X display, a D-Bus session and the
 * library preloaded. Run on a test machine, for example:
 *
 *   cc -o latetest tests/latetest.c $(pkg-config --cflags --libs gtk4 x11)
 *   for c in 1 2 3 4 5 7 8 9 11 12 13 14 15 16 17 18 19 20 21 22 \
 *            23 24 25 26 27 28 29 30 31 32 33 34 35; do
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
 *  (10 is not a case: GTK refuses an application window before the
 *      application's startup, so the library's "not registered" check is
 *      defensive only.)
 *  11  a late menu with nothing that can cross D-Bus, then a valid one
 *  12  a waiting window destroyed after the last application reference
 *      outside it was dropped
 *  13  a relabel in a section: only that section's copy changes, two
 *      Changed on D-Bus for its ids, every other id kept
 *  14  an insert at 0, one in the middle, a removal, a burst of 100 changes
 *      (one rebuild) and a change undone in its burst (nothing emitted)
 *  15  a section link switched to a new model object
 *  16  a section that cleans to nothing, then is not empty, then is again
 *  17  the filling window closed while another stays, then changes
 *  18  the owner destroyed with a rebuild pending, the application quit in
 *      that turn, the menu changed after it (run under valgrind too)
 *  19  a menu nested deeper than the copy (16 levels)
 *  20  a second window: a new widget-class action gets a stand-in there
 *  21  a menu never changed; a rebuild of 200 items, timed
 *  22  a throwaway copy from a second window, then a rebuild and a new
 *      application menu (run under valgrind too)
 * Following the shown menu (0.14):
 *  23  a stack switch to a page with a larger primary menu, and back
 *  24  the primary menu hidden while a small secondary one shows: kept
 *  25  a primary overview menu over a non-primary main menu, and back
 *  26  no primary anywhere: a page's larger menu while it shows, and back
 *  27  the owner destroyed: (a) a smaller secondary menu taken, (b) nothing
 *      shown, then a page added later, (c) the window closed
 *  28  two instances of one class: the stand-in moves to the page shown,
 *      also with the old stand-in outliving its removal
 *  29  a realize-time popover menu bar in the content comes back
 *  30  the same model behind a new owner: adopted, nothing on D-Bus
 *  31  a crossfade: at most two relooks
 *  32  list-row menu buttons mapped and unmapped: no relook
 *  33  a rebuild pending at a switch; no relook after the application
 *      takes the menubar
 *  34  relabels look at no other window; a new action looks at each once
 *  35  the window destroyed and the application quit with a relook pending
 *      (run under valgrind too)
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

/* What the D-Bus client sees under the holder, after waiting up to 3 s. The
   holder's submenu proxy is kept while waiting: a GDBusMenuModel group is
   subscribed while it has a reference, and dropping it at once would end the
   subscription before the reply arrives. */
static int client_items(void)
{
	if (client == NULL)
		return -2;
	GMenuModel *sub = NULL;
	int n = 0;
	for (int i = 0; i < 30 && n <= 0; i++) {
		if (sub == NULL && g_menu_model_get_n_items(client) > 0)
			sub = g_menu_model_get_item_link(client, 0, "submenu");
		if (sub != NULL)
			n = g_menu_model_get_n_items(sub);
		if (n <= 0)
			spin(100);
	}
	if (sub != NULL)
		g_object_unref(sub);
	return n;
}

/* Wait until WIDGET is mapped and the application's menubar has a filled
   holder, up to 3 s each. The library fills the menubar from an idle after the
   map, which runs after the next frame: under Xvfb a widget packed after
   present maps up to 0.8 s later, or at once inside a frame that itself takes
   0.3 s, so no fixed wait fits both. */
static void wait_mapped(GtkWidget *widget)
{
	for (int i = 0; i < 150 && !gtk_widget_get_mapped(widget); i++)
		spin(20);
	for (int i = 0; i < 150 && holder_items(gtk_application_get_menubar(app)) <= 0; i++)
		spin(20);
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

/* Wait up to 3 s for TEXT in the library's log, so that a case checks the path
   it names before going on. Without a log (the control run) it just waits. */
static void wait_log(const char *text)
{
	for (int i = 0; i < 150 && !log_has(text); i++)
		spin(20);
}

/* Wait up to 3 s for a filled holder in the application's menubar. */
static void wait_filled(void)
{
	for (int i = 0; i < 150 && holder_items(gtk_application_get_menubar(app)) <= 0; i++)
		spin(20);
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

/* ---- following changes (0.13) --------------------------------------------- */

static GMenuModel *kept; /* case 18: the menu, changed after the application went */

static void count_cb(GMenuModel *m, gint pos, gint removed, gint added,
		     gpointer data)
{
	(*(int *)data)++;
}

static void count_changes(GMenuModel *model, int *counter)
{
	g_signal_connect(model, "items-changed", G_CALLBACK(count_cb), counter);
}

static int log_count(const char *text)
{
	const char *path = g_getenv("UNITY_GTK4_MENU_LOG");
	char *data = NULL;
	int n = 0;

	if (path != NULL && g_file_get_contents(path, &data, NULL, NULL)) {
		for (char *p = data; (p = strstr(p, text)) != NULL; p++)
			n++;
		g_free(data);
	}
	return n;
}

/* The link of item I of MODEL, borrowed (MODEL keeps it). */
static GMenuModel *link_of(GMenuModel *model, int i, const char *link)
{
	if (model == NULL || i >= g_menu_model_get_n_items(model))
		return NULL;
	GMenuModel *out = g_menu_model_get_item_link(model, i, link);
	if (out != NULL)
		g_object_unref(out);
	return out;
}

/* Our inner menu: the holder's submenu in the application's menubar. */
static GMenuModel *ours(void)
{
	return link_of(gtk_application_get_menubar(app), 0, "submenu");
}

/* The labels of MODEL's items, comma-separated; g_free it. */
static char *labels(GMenuModel *model)
{
	GString *s = g_string_new(NULL);
	int n = model != NULL ? g_menu_model_get_n_items(model) : 0;
	for (int i = 0; i < n; i++) {
		char *label = NULL;
		g_menu_model_get_item_attribute(model, i, "label", "s", &label);
		g_string_append_printf(s, "%s%s", i ? "," : "",
				       label != NULL ? label : "-");
		g_free(label);
	}
	return g_string_free(s, FALSE);
}

static void relabel(GMenu *menu, int i, const char *label, const char *action)
{
	g_menu_remove(menu, i);
	g_menu_insert(menu, i, label, action);
}

/* The export, read the way the panel reads it: org.gtk.Menus Start on every
   group reachable from group 0, and the Changed signals counted. */
static char *export_path;
static int dbus_changes;
static GString *dbus_changed_ids;

static void call_done(GObject *source, GAsyncResult *res, gpointer data)
{
	*(GVariant **)data = g_dbus_connection_call_finish(
		G_DBUS_CONNECTION(source), res, NULL);
}

/* The exporter runs in this thread, so a call to it must not block. */
static GVariant *menus_call(const char *method, GVariant *params)
{
	GDBusConnection *bus =
		g_application_get_dbus_connection(G_APPLICATION(app));
	GVariant *reply = NULL;
	g_dbus_connection_call(bus, g_dbus_connection_get_unique_name(bus),
			       export_path, "org.gtk.Menus", method, params,
			       NULL, G_DBUS_CALL_FLAGS_NONE, 3000, NULL,
			       call_done, &reply);
	for (int i = 0; i < 300 && reply == NULL; i++)
		spin(10);
	return reply;
}

static void on_changed(GDBusConnection *c, const char *sender,
		       const char *path, const char *iface, const char *signal,
		       GVariant *params, gpointer data)
{
	GVariantIter *iter;
	guint group, menu, pos, removed;
	GVariantIter *added;

	g_variant_get(params, "(a(uuuuaa{sv}))", &iter);
	while (g_variant_iter_next(iter, "(uuuuaa{sv})", &group, &menu, &pos,
				   &removed, &added)) {
		dbus_changes++;
		g_string_append_printf(dbus_changed_ids, "%u/%u ", group, menu);
		g_variant_iter_free(added);
	}
	g_variant_iter_free(iter);
}

static void watch_export(GtkWidget *window)
{
	export_path = menubar_path(window);
	if (export_path == NULL)
		return;
	GDBusConnection *bus =
		g_application_get_dbus_connection(G_APPLICATION(app));
	dbus_changed_ids = g_string_new(NULL);
	g_dbus_connection_signal_subscribe(
		bus, g_dbus_connection_get_unique_name(bus), "org.gtk.Menus",
		"Changed", export_path, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
		on_changed, NULL, NULL);
}

static void reset_changes(void)
{
	dbus_changes = 0;
	if (dbus_changed_ids != NULL)
		g_string_truncate(dbus_changed_ids, 0);
}

/* Every item of every group reachable from group 0, one line each:
   "group/menu/position label :section=g/m :submenu=g/m". With LINKS_ONLY the
   labels are left out, to compare ids across a relabel. */
static char *export_dump(int links_only)
{
	GString *out = g_string_new(NULL);
	GArray *started = g_array_new(FALSE, FALSE, sizeof(guint));
	GArray *todo = g_array_new(FALSE, FALSE, sizeof(guint));
	guint zero = 0;

	if (export_path == NULL)
		return g_string_free(out, FALSE);
	g_array_append_val(todo, zero);
	for (int round = 0; round < 64 && todo->len > 0; round++) {
		GVariantBuilder b;
		g_variant_builder_init(&b, G_VARIANT_TYPE("au"));
		for (guint i = 0; i < todo->len; i++) {
			g_variant_builder_add(&b, "u", g_array_index(todo, guint, i));
			g_array_append_val(started, g_array_index(todo, guint, i));
		}
		g_array_set_size(todo, 0);

		GVariant *reply = menus_call("Start", g_variant_new("(au)", &b));
		if (reply == NULL)
			break;
		GVariantIter *menus, *items;
		guint group, menu;
		g_variant_get(reply, "(a(uuaa{sv}))", &menus);
		while (g_variant_iter_next(menus, "(uuaa{sv})", &group, &menu,
					   &items)) {
			GVariant *item;
			for (int pos = 0; (item = g_variant_iter_next_value(items));
			     pos++) {
				const char *label = NULL;
				guint lg, lm;
				g_string_append_printf(out, "%u/%u/%d", group, menu, pos);
				if (!links_only &&
				    g_variant_lookup(item, "label", "&s", &label))
					g_string_append_printf(out, " %s", label);
				if (g_variant_lookup(item, ":section", "(uu)", &lg, &lm))
					g_string_append_printf(out, " :section=%u/%u", lg, lm);
				if (g_variant_lookup(item, ":submenu", "(uu)", &lg, &lm)) {
					g_string_append_printf(out, " :submenu=%u/%u", lg, lm);
					int seen = 0;
					for (guint i = 0; i < started->len; i++)
						seen |= g_array_index(started, guint, i) == lg;
					for (guint i = 0; i < todo->len; i++)
						seen |= g_array_index(todo, guint, i) == lg;
					if (!seen)
						g_array_append_val(todo, lg);
				}
				g_string_append_c(out, '\n');
				g_variant_unref(item);
			}
			g_variant_iter_free(items);
		}
		g_variant_iter_free(menus);
		g_variant_unref(reply);
	}
	g_array_unref(started);
	g_array_unref(todo);
	return g_string_free(out, FALSE);
}

static int export_has(const char *label)
{
	char *dump = export_dump(0);
	char *needle = g_strdup_printf(" %s\n", label);
	int found = strstr(dump, needle) != NULL;
	g_free(needle);
	g_free(dump);
	return found;
}

/* A window with a menu button for MENU in its header bar at realize, filled. */
static GtkWidget *filled_window(GMenuModel *menu, GtkWidget **button)
{
	GtkWidget *header;
	GtkWidget *window = app_window_with_header(&header);
	GtkWidget *b = menu_button(menu);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(header), b);
	gtk_window_present(GTK_WINDOW(window));
	wait_mapped(b);
	spin(200);
	if (button != NULL)
		*button = b;
	return window;
}

/* Wait for the library's Nth rebuild, then let the exporter's signals in. */
static void wait_rebuild(int n)
{
	char *text = g_strdup_printf("menu changed (rebuild %d)", n);
	wait_log(text);
	g_free(text);
	spin(300);
}

#define CHECK_REBUILDS(n)                                                       \
	do {                                                                    \
		char *t_ = g_strdup_printf("menu changed (rebuild %d)", (n));   \
		CHECK_LOG(1, t_);                                               \
		g_free(t_);                                                     \
		t_ = g_strdup_printf("menu changed (rebuild %d)", (n) + 1);     \
		CHECK_LOG(0, t_);                                               \
		g_free(t_);                                                     \
	} while (0)

/* Case 20: a container whose class installs an action, as yelp's does. */
static GtkWidget *activated_on;

static void demo_act(GtkWidget *widget, const char *name, GVariant *param)
{
	activated_on = widget;
}

static void demo_box_class_init(gpointer klass, gpointer data)
{
	gtk_widget_class_install_action(GTK_WIDGET_CLASS(klass), "demo.act",
					NULL, demo_act);
}

static GType demo_box_type(void)
{
	static GType type;
	if (type == 0) {
		GTypeQuery q;
		g_type_query(GTK_TYPE_BOX, &q);
		type = g_type_register_static_simple(GTK_TYPE_BOX, "LateTestDemoBox",
						     q.class_size,
						     demo_box_class_init,
						     q.instance_size, NULL, 0);
	}
	return type;
}


/* ---- following the shown menu (0.14) -------------------------------------- */

/* A menu of N items "PREFIX-i" naming app.new, the kind a main menu is made
   of; with GROUP_ITEMS more naming grp.x, an inserted group's kind. */
static GMenuModel *menu_n(const char *prefix, int n, int group_items)
{
	GMenu *s = g_menu_new();
	for (int i = 0; i < n; i++) {
		char *l = g_strdup_printf("%s-%d", prefix, i);
		g_menu_append(s, l, "app.new");
		g_free(l);
	}
	for (int i = 0; i < group_items; i++)
		g_menu_append(s, "Group item", "grp.x");
	return G_MENU_MODEL(s);
}

/* A page as papers builds one: content with its own header bar holding a
   menu button for MODEL. */
static GtkWidget *page_with_menu(GMenuModel *model, gboolean primary,
				 GtkWidget **button)
{
	GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget *header = gtk_header_bar_new();
	gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(header), FALSE);
	GtkWidget *b = menu_button(model);
	gtk_menu_button_set_primary(GTK_MENU_BUTTON(b), primary);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(header), b);
	gtk_box_append(GTK_BOX(page), header);
	gtk_box_append(GTK_BOX(page), gtk_label_new("content"));
	if (button != NULL)
		*button = b;
	return page;
}

/* An application window whose content is a stack of PAGES, the first shown. */
static GtkWidget *stack_window(GtkWidget **stack, GtkWidget **pages, int n)
{
	GtkWidget *window = gtk_application_window_new(app);
	gtk_window_set_default_size(GTK_WINDOW(window), 300, 200);
	*stack = gtk_stack_new();
	for (int i = 0; i < n; i++) {
		char name[16];
		snprintf(name, sizeof name, "p%d", i + 1);
		gtk_stack_add_named(GTK_STACK(*stack), pages[i], name);
	}
	gtk_window_set_child(GTK_WINDOW(window), *stack);
	return window;
}

static void show_page(GtkWidget *stack, const char *name)
{
	gtk_stack_set_visible_child_name(GTK_STACK(stack), name);
}

/* The first label of our copy, "" if none; g_free it. */
static char *ours_first(void)
{
	GMenuModel *m = ours();
	char *label = NULL;
	if (m != NULL && g_menu_model_get_n_items(m) > 0)
		g_menu_model_get_item_attribute(m, 0, "label", "s", &label);
	return label != NULL ? label : g_strdup("");
}

#define CHECK_FIRST(want)                                                       \
	do {                                                                    \
		char *f_ = ours_first();                                        \
		CHECK(strcmp(f_, (want)) == 0, "the copy starts with \"%s\", "  \
		      "not \"%s\"", f_, (want));                                \
		g_free(f_);                                                     \
	} while (0)

/* Wait for the Nth occurrence of TEXT in the library's log, then settle. */
static void wait_log_n(const char *text, int n)
{
	for (int i = 0; i < 150 && log_count(text) < n; i++)
		spin(20);
	spin(200);
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
			wait_mapped(b);
		}
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
		{
			GtkWidget *bar = gtk_popover_menu_bar_new_from_model(menu);
			gtk_header_bar_pack_start(GTK_HEADER_BAR(header), bar);
			wait_mapped(bar);
		}
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
			/* the map of the empty button makes a look that finds
			   nothing; the model given after it must come through
			   the per-instance notify, not through the map */
			wait_log("late look: no menu yet");
			CHECK_LOG(1, "late look: no menu yet");
			CHECK(holder_items(gtk_application_get_menubar(app)) == 0,
			      "items before the button has a menu");
			gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(b), menu);
		}
		wait_filled();
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
		/* 0.14 installs the map shims at the fill too, to follow the
		   shown menu; the window still never waits */
		CHECK_LOG(1, "hooked GtkMenuButton::map");
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
		/* the first look must see the menu that cleans to nothing
		   on its own, before the valid one exists */
		wait_log("has no item that can cross D-Bus");
		CHECK_LOG(1, "has no item that can cross D-Bus");
		CHECK(holder_items(gtk_application_get_menubar(app)) == 0,
		      "an empty cleaned menu was used");
		gtk_header_bar_pack_start(GTK_HEADER_BAR(header), menu_button(menu));
		wait_filled();
		CHECK(holder_items(gtk_application_get_menubar(app)) > 0,
		      "the valid menu after an empty one was not used");
		CHECK_LOG(1, "menubar filled late");
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

	case 13: { /* a relabel in a section */
		GMenu *a = g_menu_new(), *b = g_menu_new(), *file = g_menu_new();
		g_menu_append(a, "One", "app.new");
		g_menu_append(a, "Two", "app.quit");
		g_menu_append(b, "Undo", "app.new");
		g_menu_append(file, "New", "app.new");
		GMenu *top = g_menu_new();
		g_menu_append_section(top, NULL, G_MENU_MODEL(a));
		g_menu_append_section(top, NULL, G_MENU_MODEL(b));
		g_menu_append_submenu(top, "File", G_MENU_MODEL(file));
		window = filled_window(G_MENU_MODEL(top), NULL);
		watch_export(window);
		char *before = export_dump(1);
		char *full = export_dump(0);
		/* the section B copy's group/menu, from the line naming Undo */
		char *ids = NULL, *line = strstr(full, " Undo\n");
		while (line != NULL && line > full && line[-1] != '\n')
			line--;
		char *slash = line != NULL ? strchr(line, '/') : NULL;
		if (slash != NULL && (slash = strchr(slash + 1, '/')) != NULL)
			ids = g_strndup(line, slash - line);
		int on_inner = 0, on_container = 0, on_section = 0;
		count_changes(gtk_application_get_menubar(app), &on_container);
		count_changes(ours(), &on_inner);
		count_changes(link_of(ours(), 1, "section"), &on_section);
		reset_changes();

		relabel(b, 0, "Undo Rename", "app.new");
		wait_rebuild(1);
		CHECK(on_section == 2, "items-changed on our section: %d, not 2",
		      on_section);
		CHECK(on_inner == 0 && on_container == 0,
		      "items-changed on inner %d, container %d, not 0", on_inner,
		      on_container);
		CHECK(dbus_changes == 2, "%d Changed on D-Bus, not 2", dbus_changes);
		if (ids != NULL) {
			char *two = g_strdup_printf("%s %s ", ids, ids);
			CHECK(strcmp(dbus_changed_ids->str, two) == 0,
			      "Changed for \"%s\", not for the section %s",
			      dbus_changed_ids->str, ids);
			g_free(two);
		}
		char *after = export_dump(1);
		CHECK(strcmp(before, after) == 0, "ids changed:\n%s---\n%s", before,
		      after);
		CHECK(export_has("Undo Rename"), "the new label is not exported");
		CHECK(!export_has("Undo"), "the old label is still exported");
		CHECK_REBUILDS(1);
		CHECK_LOG(1, "rebuild 1): 1 item(s) replaced");
		g_free(before);
		g_free(after);
		g_free(full);
		g_free(ids);
		break;
	}

	case 14: { /* inserts, a removal, a burst, an undone change */
		GMenu *s = g_menu_new(), *file = g_menu_new();
		const char *five[] = { "A", "B", "C", "D", "E" };
		for (int i = 0; i < 5; i++)
			g_menu_append(s, five[i], "app.new");
		g_menu_append(file, "New", "app.new");
		GMenu *top = g_menu_new();
		g_menu_append_section(top, NULL, G_MENU_MODEL(s));
		g_menu_append_submenu(top, "File", G_MENU_MODEL(file));
		window = filled_window(G_MENU_MODEL(top), NULL);
		watch_export(window);
		g_free(export_dump(0));
		int on_ours = 0, on_inner = 0;
		GMenuModel *copy = link_of(ours(), 0, "section");
		count_changes(copy, &on_ours);
		count_changes(ours(), &on_inner);
		struct {
			const char *what, *expect;
			int changes;
		} steps[] = {
			{ "an insert at 0", "Z,A,B,C,D,E", 1 },
			{ "an insert in the middle", "Z,A,B,C,M,D,E", 1 },
			{ "a removal", "Z,A,B,C,D,E", 1 },
			{ "a burst of 100", "Y99,A,B,C,D,E", 2 },
			{ "a change undone in the burst", "Y99,A,B,C,D,E", 0 },
		};
		for (int k = 0; k < 5; k++) {
			on_ours = on_inner = 0;
			reset_changes();
			switch (k) {
			case 0: g_menu_insert(s, 0, "Z", "app.new"); break;
			case 1: g_menu_insert(s, 4, "M", "app.new"); break;
			case 2: g_menu_remove(s, 4); break;
			case 3:
				for (int i = 0; i < 100; i++) {
					char *l = g_strdup_printf("Y%d", i);
					relabel(s, 0, l, "app.new");
					g_free(l);
				}
				break;
			case 4:
				g_menu_remove(s, 0);
				g_menu_insert(s, 0, "Y99", "app.new");
				break;
			}
			wait_rebuild(k + 1);
			char *got = labels(copy);
			CHECK(strcmp(got, steps[k].expect) == 0, "%s: %s, not %s",
			      steps[k].what, got, steps[k].expect);
			g_free(got);
			CHECK(on_ours == steps[k].changes,
			      "%s: %d items-changed on our section, not %d",
			      steps[k].what, on_ours, steps[k].changes);
			CHECK(dbus_changes == steps[k].changes,
			      "%s: %d Changed on D-Bus, not %d", steps[k].what,
			      dbus_changes, steps[k].changes);
			CHECK(on_inner == 0, "%s: inner changed", steps[k].what);
		}
		CHECK_REBUILDS(5);
		CHECK_LOG(1, "rebuild 5): 0 item(s) replaced");
		CHECK(link_of(ours(), 0, "section") == copy,
		      "our section copy was replaced");
		break;
	}

	case 15: { /* a section link switched to a new model object */
		GMenu *s1 = g_menu_new(), *s2 = g_menu_new(), *file = g_menu_new();
		g_menu_append(s1, "One", "app.new");
		g_menu_append(s2, "Two", "app.new");
		g_menu_append(file, "New", "app.new");
		GMenu *top = g_menu_new();
		g_menu_append_section(top, NULL, G_MENU_MODEL(s1));
		g_menu_append_submenu(top, "File", G_MENU_MODEL(file));
		window = filled_window(G_MENU_MODEL(top), NULL);
		g_menu_remove(top, 0);
		g_menu_insert_section(top, 0, NULL, G_MENU_MODEL(s2));
		wait_rebuild(1);
		char *got = labels(link_of(ours(), 0, "section"));
		CHECK(strcmp(got, "Two") == 0, "after the switch: %s", got);
		g_free(got);
		relabel(s1, 0, "One changed", "app.new");
		spin(500);
		CHECK_LOG(0, "menu changed (rebuild 2)");
		relabel(s2, 0, "Three", "app.new");
		wait_rebuild(2);
		got = labels(link_of(ours(), 0, "section"));
		CHECK(strcmp(got, "Three") == 0, "after a change on the new one: %s",
		      got);
		g_free(got);
		CHECK_REBUILDS(2);
		break;
	}

	case 16: { /* a section that cleans to nothing, then is not empty */
		GMenu *e = g_menu_new(), *n = g_menu_new();
		GMenuItem *item = g_menu_item_new("Custom", "app.new");
		g_menu_item_set_attribute(item, "custom", "s", "widget");
		g_menu_append_item(e, item);
		g_object_unref(item);
		g_menu_append(n, "One", "app.new");
		GMenu *top = g_menu_new();
		g_menu_append_section(top, NULL, G_MENU_MODEL(e));
		g_menu_append_section(top, NULL, G_MENU_MODEL(n));
		window = filled_window(G_MENU_MODEL(top), NULL);
		CHECK(g_menu_model_get_n_items(ours()) == 1,
		      "the empty section was exported");
		g_menu_append(e, "Extra", "app.new");
		wait_rebuild(1);
		CHECK(g_menu_model_get_n_items(ours()) == 2,
		      "the section that became non-empty is missing");
		char *got = labels(link_of(ours(), 0, "section"));
		CHECK(strcmp(got, "Extra") == 0, "the section: %s", got);
		g_free(got);
		g_menu_remove(e, 1);
		wait_rebuild(2);
		CHECK(g_menu_model_get_n_items(ours()) == 1,
		      "the section emptied again is still there");
		got = labels(link_of(ours(), 0, "section"));
		CHECK(strcmp(got, "One") == 0, "the remaining section: %s", got);
		g_free(got);
		break;
	}

	case 17: { /* the filling window closed while another stays */
		GMenu *s = g_menu_new();
		g_menu_append(s, "One", "app.new");
		GMenu *top = g_menu_new();
		g_menu_append_section(top, NULL, G_MENU_MODEL(s));
		window = filled_window(G_MENU_MODEL(top), NULL);
		GtkWidget *other = gtk_application_window_new(app);
		gtk_window_present(GTK_WINDOW(other));
		spin(300);
		gtk_window_destroy(GTK_WINDOW(window));
		spin(300);
		relabel(s, 0, "Changed", "app.new");
		spin(500);
		relabel(s, 0, "Changed again", "app.new");
		spin(500);
		CHECK_LOG(0, "menu changed");
		if (!control)
			CHECK(log_count("no longer followed") == 1,
			      "%d \"no longer followed\" lines, not 1",
			      log_count("no longer followed"));
		char *got = labels(link_of(ours(), 0, "section"));
		CHECK(strcmp(got, "One") == 0, "the copy changed: %s", got);
		g_free(got);
		break;
	}

	case 18: { /* the owner gone with an idle pending; quit in that turn */
		GMenu *s = g_menu_new();
		g_menu_append(s, "One", "app.new");
		GMenu *top = g_menu_new();
		g_menu_append_section(top, NULL, G_MENU_MODEL(s));
		window = filled_window(G_MENU_MODEL(top), NULL);
		kept = G_MENU_MODEL(g_object_ref(s));
		relabel(s, 0, "Changed", "app.new");
		gtk_window_destroy(GTK_WINDOW(window));
		break; /* main() changes it again after the application */
	}

	case 19: { /* nested deeper than the copy */
		enum { DEEP = 20 };
		GMenu *m[DEEP + 1];
		for (int k = 0; k <= DEEP; k++)
			m[k] = g_menu_new();
		for (int k = DEEP; k >= 0; k--) {
			char *l = g_strdup_printf("L%d", k);
			g_menu_append(m[k], l, "app.new");
			g_free(l);
			if (k < DEEP) {
				l = g_strdup_printf("S%d", k + 1);
				g_menu_append_submenu(m[k], l, G_MENU_MODEL(m[k + 1]));
				g_free(l);
			}
		}
		window = filled_window(G_MENU_MODEL(m[0]), NULL);
		watch_export(window);
		CHECK(export_has("L18"), "the deep item is not exported");
		int c16 = 0, c17 = 0, c18 = 0;
		count_changes(G_MENU_MODEL(m[16]), &c16);
		count_changes(G_MENU_MODEL(m[17]), &c17);
		count_changes(G_MENU_MODEL(m[18]), &c18);

		/* below the copy: the exporter follows it, we do not */
		relabel(m[18], 0, "L18 changed", "app.new");
		spin(500);
		CHECK_LOG(0, "menu changed");
		CHECK(export_has("L18 changed"),
		      "a change below the copy does not reach the client");
		CHECK(c16 == 0 && c17 == 0 && c18 == 2,
		      "the application's models were edited: %d %d %d", c16, c17,
		      c18);

		/* the deepest copied level gets a new link object */
		GMenu *m17b = g_menu_new();
		g_menu_append(m17b, "L17b", "app.new");
		g_menu_remove(m[16], 1);
		g_menu_insert_submenu(m[16], 1, "S17", G_MENU_MODEL(m17b));
		wait_rebuild(1);
		CHECK_LOG(1, "rebuild 1): 1 item(s) replaced");
		CHECK(export_has("L17b"), "the new link object is not exported");
		CHECK(c16 == 2 && c17 == 0, "the application's models edited: %d %d",
		      c16, c17);
		/* a relabel at the deepest copied level, same link object */
		relabel(m[16], 0, "L16 changed", "app.new");
		wait_rebuild(2);
		CHECK_LOG(1, "rebuild 2): 1 item(s) replaced");
		CHECK(export_has("L16 changed"), "the relabel at 16 is not exported");
		CHECK(export_has("L17b"), "the link at 16 was lost");
		CHECK(c16 == 4, "the application's model at 16 edited: %d", c16);
		g_object_unref(m17b);
		break;
	}

	case 20: { /* a second window: a new action gets its stand-in there */
		GMenu *s = g_menu_new();
		g_menu_append(s, "One", "app.new");
		GMenu *top = g_menu_new();
		g_menu_append_section(top, NULL, G_MENU_MODEL(s));
		GtkWidget *w[2], *box[2];
		for (int k = 0; k < 2; k++) {
			w[k] = app_window_with_header(&header);
			box[k] = g_object_new(demo_box_type(), NULL);
			gtk_box_append(GTK_BOX(box[k]), menu_button(G_MENU_MODEL(top)));
			gtk_header_bar_pack_end(GTK_HEADER_BAR(header), box[k]);
			gtk_window_present(GTK_WINDOW(w[k]));
			wait_filled();
			spin(300);
		}
		const char *standin = "unity-gtk4-menu-demo-act";
		CHECK(g_action_map_lookup_action(G_ACTION_MAP(w[1]), standin) == NULL,
		      "a stand-in before the action is in the menu");
		g_menu_append(s, "Act", "demo.act");
		wait_rebuild(1);
		for (int k = 0; k < 2; k++) {
			CHECK(g_action_map_lookup_action(G_ACTION_MAP(w[k]), standin) != NULL,
			      "no stand-in in window %d", k + 1);
			activated_on = NULL;
			g_action_group_activate_action(G_ACTION_GROUP(w[k]), standin, NULL);
			spin(100);
			CHECK(activated_on == box[k],
			      "window %d's stand-in activates %p, not its own %p", k + 1,
			      (void *)activated_on, (void *)box[k]);
		}
		break;
	}

	case 21: { /* never changed; a rebuild of 200 items, timed */
		GMenu *s = g_menu_new();
		for (int i = 0; i < 200; i++) {
			char *l = g_strdup_printf("Item %d", i);
			g_menu_append(s, l, "app.new");
			g_free(l);
		}
		GMenu *top = g_menu_new();
		g_menu_append_section(top, NULL, G_MENU_MODEL(s));
		window = filled_window(G_MENU_MODEL(top), NULL);
		spin(1000);
		CHECK_LOG(0, "menu changed");
		gint64 t0 = g_get_monotonic_time();
		relabel(s, 100, "Item 100 changed", "app.new");
		while (!log_has("menu changed (rebuild 1)") &&
		       g_get_monotonic_time() - t0 < 3000000)
			g_main_context_iteration(NULL, FALSE);
		gint64 us = g_get_monotonic_time() - t0;
		fprintf(stderr, "case 21: the change to the rebuild's log line: %.1f ms\n",
			us / 1000.0);
		CHECK_LOG(1, "rebuild 1): 1 item(s) replaced");
		CHECK(control || us < 200000, "a rebuild of 200 items took %.1f ms",
		      us / 1000.0);
		break;
	}

	case 22: { /* a throwaway copy, then a rebuild and a new application menu */
		GMenu *s = g_menu_new();
		g_menu_append(s, "One", "app.new");
		GMenu *top = g_menu_new();
		g_menu_append_section(top, NULL, G_MENU_MODEL(s));
		filled_window(G_MENU_MODEL(top), NULL);
		/* the second window's realize makes a copy and drops it */
		filled_window(G_MENU_MODEL(top), NULL);
		CHECK_LOG(1, "stand-ins added for this window");
		relabel(s, 0, "Changed", "app.new");
		wait_rebuild(1);
		/* new application menus, which may take a freed copy's address */
		GMenu *fresh[32];
		for (int i = 0; i < 32; i++)
			fresh[i] = g_menu_new();
		int on_fresh = 0;
		g_menu_append(fresh[0], "Fresh", "app.new");
		count_changes(G_MENU_MODEL(fresh[0]), &on_fresh);
		g_menu_remove(top, 0);
		g_menu_insert_section(top, 0, NULL, G_MENU_MODEL(fresh[0]));
		wait_rebuild(2);
		char *got = labels(link_of(ours(), 0, "section"));
		CHECK(strcmp(got, "Fresh") == 0, "the copy: %s", got);
		g_free(got);
		CHECK(on_fresh == 0, "the application's new menu was edited");
		CHECK(g_object_get_data(G_OBJECT(fresh[0]), "unity-gtk4-menu-copy") == NULL,
		      "the application's menu carries the copy mark");
		for (int i = 0; i < 32; i++)
			g_object_unref(fresh[i]);
		break;
	}

	case 23: { /* a stack switch: page 2's primary menu replaces page 1's */
		GtkWidget *stack, *pages[2];
		pages[0] = page_with_menu(menu_n("P1", 3, 0), TRUE, NULL);
		pages[1] = page_with_menu(menu_n("P2", 5, 0), TRUE, NULL);
		window = stack_window(&stack, pages, 2);
		gtk_window_present(GTK_WINDOW(window));
		wait_filled();
		spin(300);
		CHECK_FIRST("P1-0");
		watch_export(window);
		g_free(export_dump(0)); /* subscribes every group, as the panel */
		GMenuModel *inner = ours();
		reset_changes();
		show_page(stack, "p2");
		wait_log_n("switched to the shown menu", 1);
		CHECK_FIRST("P2-0");
		CHECK(g_menu_model_get_n_items(ours()) == 5, "%d items, not 5",
		      g_menu_model_get_n_items(ours()));
		CHECK(ours() == inner, "the holder's submenu was replaced");
		CHECK(g_menu_model_get_n_items(gtk_application_get_menubar(app)) == 1,
		      "the menubar has %d holders",
		      g_menu_model_get_n_items(gtk_application_get_menubar(app)));
		CHECK(dbus_changes > 0, "no Changed on D-Bus");
		CHECK(export_has("P2-4"), "the export lacks P2-4");
		show_page(stack, "p1");
		wait_log_n("switched to the shown menu", 2);
		CHECK_FIRST("P1-0");
		break;
	}

	case 24: { /* the primary menu hidden: a shown secondary one is not taken */
		GtkWidget *b_main, *b_doc;
		GtkWidget *page = page_with_menu(menu_n("Main", 5, 0), TRUE, &b_main);
		GtkWidget *header = gtk_widget_get_first_child(page);
		b_doc = menu_button(menu_n("Doc", 1, 10));
		gtk_header_bar_pack_start(GTK_HEADER_BAR(header), b_doc);
		window = gtk_application_window_new(app);
		gtk_window_set_child(GTK_WINDOW(window), page);
		gtk_window_present(GTK_WINDOW(window));
		wait_filled();
		spin(300);
		CHECK_FIRST("Main-0");
		gtk_widget_set_visible(b_main, FALSE);
		wait_log_n("relook", 1);
		CHECK_LOG(1, "relook: the menu followed is kept");
		CHECK_LOG(1, "shown=1 primary=0 app./win. items=1");
		CHECK_FIRST("Main-0");
		gtk_widget_set_visible(b_main, TRUE);
		wait_log_n("relook", 2);
		CHECK_LOG(0, "switched to the shown menu");
		CHECK_FIRST("Main-0");
		break;
	}

	case 25: { /* Console's shape: a primary overview menu, then back */
		GtkWidget *stack, *pages[2];
		pages[0] = page_with_menu(menu_n("Main", 6, 0), FALSE, NULL);
		pages[1] = page_with_menu(menu_n("Overview", 3, 0), TRUE, NULL);
		window = stack_window(&stack, pages, 2);
		gtk_window_present(GTK_WINDOW(window));
		wait_filled();
		spin(300);
		CHECK_FIRST("Main-0");
		show_page(stack, "p2");
		wait_log_n("switched to the shown menu", 1);
		CHECK_FIRST("Overview-0");
		show_page(stack, "p1");
		wait_log_n("switched to the shown menu", 2);
		CHECK_FIRST("Main-0");
		break;
	}

	case 26: { /* no primary anywhere: a page's larger menu while it shows */
		GtkWidget *stack, *pages[2];
		pages[0] = gtk_label_new("no menu here");
		pages[1] = page_with_menu(menu_n("Page", 8, 0), FALSE, NULL);
		window = stack_window(&stack, pages, 2);
		header = gtk_header_bar_new();
		gtk_header_bar_pack_end(GTK_HEADER_BAR(header),
					menu_button(menu_n("Main", 6, 0)));
		gtk_window_set_titlebar(GTK_WINDOW(window), header);
		gtk_window_present(GTK_WINDOW(window));
		wait_filled();
		spin(300);
		CHECK_FIRST("Main-0");
		show_page(stack, "p2");
		wait_log_n("switched to the shown menu", 1);
		CHECK_FIRST("Page-0");
		show_page(stack, "p1");
		wait_log_n("switched to the shown menu", 2);
		CHECK_FIRST("Main-0");
		break;
	}

	case 27: { /* the owner destroyed, not hidden */
		GtkWidget *stack, *pages[2];
		pages[0] = page_with_menu(menu_n("Gone", 3, 0), TRUE, NULL);
		pages[1] = page_with_menu(menu_n("Small", 2, 0), FALSE, NULL);
		window = stack_window(&stack, pages, 2);
		gtk_window_present(GTK_WINDOW(window));
		wait_filled();
		spin(300);
		CHECK_FIRST("Gone-0");
		/* (a) the current page destroyed, a smaller secondary menu shown */
		gtk_stack_remove(GTK_STACK(stack), pages[0]);
		show_page(stack, "p2");
		wait_log_n("switched to the shown menu", 1);
		CHECK_FIRST("Small-0");
		/* (b) that page destroyed too, nothing else: orphaned, then a
		   page added after the idle has run */
		gtk_stack_remove(GTK_STACK(stack), pages[1]);
		wait_log_n("no other menu is shown; waiting", 1);
		CHECK_LOG(1, "no other menu is shown; waiting");
		CHECK_FIRST("Small-0");
		spin(500);
		GtkWidget *fresh = page_with_menu(menu_n("Later", 4, 0), TRUE, NULL);
		gtk_stack_add_named(GTK_STACK(stack), fresh, "p3");
		show_page(stack, "p3");
		wait_log_n("switched to the shown menu", 2);
		CHECK_FIRST("Later-0");
		/* (c) the window closed: no longer followed */
		gtk_window_destroy(GTK_WINDOW(window));
		spin(300);
		CHECK_LOG(1, "the menu's window is gone; the menu is no longer followed");
		break;
	}

	case 28: { /* two instances of one class: the stand-in moves with the menu */
		GtkWidget *stack, *pages[2], *box[2];
		for (int k = 0; k < 2; k++) {
			GMenu *m = g_menu_new();
			g_menu_append(m, k ? "Second" : "First", "app.new");
			g_menu_append(m, "Act", "demo.act");
			box[k] = g_object_new(demo_box_type(), NULL);
			gtk_box_append(GTK_BOX(box[k]),
				       page_with_menu(G_MENU_MODEL(m), TRUE, NULL));
			pages[k] = box[k];
		}
		window = stack_window(&stack, pages, 2);
		gtk_window_present(GTK_WINDOW(window));
		wait_filled();
		spin(300);
		const char *standin = "unity-gtk4-menu-demo-act";
		GAction *old = g_action_map_lookup_action(G_ACTION_MAP(window), standin);
		CHECK(old != NULL, "no stand-in after the fill");
		if (old != NULL)
			g_object_ref(old); /* outlives its removal */
		show_page(stack, "p2");
		wait_log_n("switched to the shown menu", 1);
		CHECK_LOG(1, "stand-in moved to the menu shown now");
		activated_on = NULL;
		g_action_group_activate_action(G_ACTION_GROUP(window), standin, NULL);
		spin(100);
		CHECK(activated_on == box[1], "the stand-in activates %p, not page 2's %p",
		      (void *)activated_on, (void *)box[1]);
		GAction *now = g_action_map_lookup_action(G_ACTION_MAP(window), standin);
		CHECK(now != NULL && now != old, "the stand-in was not replaced");
		gtk_widget_action_set_enabled(box[1], "demo.act", FALSE);
		spin(100);
		CHECK(now != NULL && !g_action_get_enabled(now),
		      "the new stand-in does not follow page 2's enabled state");
		if (old != NULL)
			g_object_unref(old); /* its proxy is freed only now */
		spin(100);
		gtk_widget_action_set_enabled(box[1], "demo.act", TRUE);
		spin(100);
		now = g_action_map_lookup_action(G_ACTION_MAP(window), standin);
		CHECK(now != NULL && g_action_get_enabled(now),
		      "the old stand-in's end cut the new one off");
		break;
	}

	case 29: { /* a realize-time menu outside header bars comes back */
		GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		gtk_box_append(GTK_BOX(content),
			       gtk_popover_menu_bar_new_from_model(menu_n("Bar", 3, 0)));
		window = gtk_application_window_new(app);
		gtk_window_set_child(GTK_WINDOW(window), content);
		header = gtk_header_bar_new();
		GtkWidget *b = menu_button(menu_n("Head", 3, 0));
		gtk_menu_button_set_primary(GTK_MENU_BUTTON(b), TRUE);
		gtk_widget_set_visible(b, FALSE);
		gtk_header_bar_pack_end(GTK_HEADER_BAR(header), b);
		gtk_window_set_titlebar(GTK_WINDOW(window), header);
		gtk_window_present(GTK_WINDOW(window));
		wait_filled();
		spin(300);
		CHECK_FIRST("Bar-0");
		gtk_widget_set_visible(b, TRUE);
		wait_log_n("switched to the shown menu", 1);
		CHECK_FIRST("Head-0");
		gtk_widget_set_visible(b, FALSE);
		wait_log_n("switched to the shown menu", 2);
		CHECK_FIRST("Bar-0");
		break;
	}

	case 30: { /* the same model behind a new owner */
		GMenu *m = g_menu_new();
		g_menu_append(m, "Same", "app.new");
		g_menu_append(m, "Act", "demo.act");
		window = gtk_application_window_new(app);
		header = gtk_header_bar_new();
		gtk_window_set_titlebar(GTK_WINDOW(window), header);
		GtkWidget *box1 = g_object_new(demo_box_type(), NULL);
		gtk_box_append(GTK_BOX(box1), menu_button(G_MENU_MODEL(m)));
		gtk_header_bar_pack_end(GTK_HEADER_BAR(header), box1);
		gtk_window_present(GTK_WINDOW(window));
		wait_filled();
		spin(300);
		watch_export(window);
		g_free(export_dump(0)); /* subscribes every group, as the panel */
		reset_changes();
		gtk_header_bar_remove(GTK_HEADER_BAR(header), box1);
		GtkWidget *box2 = g_object_new(demo_box_type(), NULL);
		gtk_box_append(GTK_BOX(box2), menu_button(G_MENU_MODEL(m)));
		gtk_header_bar_pack_end(GTK_HEADER_BAR(header), box2);
		wait_log_n("owner adopted", 1);
		CHECK_LOG(1, "owner adopted: 0 item(s) replaced");
		CHECK(dbus_changes == 0, "%d Changed on D-Bus", dbus_changes);
		activated_on = NULL;
		g_action_group_activate_action(G_ACTION_GROUP(window),
					       "unity-gtk4-menu-demo-act", NULL);
		spin(100);
		CHECK(activated_on == box2, "activation reached %p, not the new %p",
		      (void *)activated_on, (void *)box2);
		break;
	}

	case 31: { /* a crossfade: at most two relooks, page 2's menu at the end */
		GtkWidget *stack, *pages[2];
		pages[0] = page_with_menu(menu_n("P1", 3, 0), TRUE, NULL);
		pages[1] = page_with_menu(menu_n("P2", 5, 0), TRUE, NULL);
		window = stack_window(&stack, pages, 2);
		gtk_stack_set_transition_type(GTK_STACK(stack),
					      GTK_STACK_TRANSITION_TYPE_CROSSFADE);
		gtk_stack_set_transition_duration(GTK_STACK(stack), 200);
		gtk_window_present(GTK_WINDOW(window));
		wait_filled();
		spin(300);
		show_page(stack, "p2");
		spin(1000);
		CHECK_FIRST("P2-0");
		if (!control)
			CHECK(log_count("] relook\n") >= 1 && log_count("] relook\n") <= 2,
			      "%d relooks", log_count("] relook\n"));
		break;
	}

	case 32: { /* list-row menu buttons mapped and unmapped: no relook */
		GtkWidget *page = page_with_menu(menu_n("Main", 2, 0), TRUE, NULL);
		GtkWidget *list = gtk_list_box_new();
		for (int i = 0; i < 200; i++)
			gtk_list_box_append(GTK_LIST_BOX(list),
					    menu_button(menu_n("Row", 1, 0)));
		gtk_box_append(GTK_BOX(page), list);
		window = gtk_application_window_new(app);
		gtk_window_set_child(GTK_WINDOW(window), page);
		gtk_window_present(GTK_WINDOW(window));
		wait_filled();
		spin(300);
		for (int k = 0; k < 5; k++) {
			gtk_widget_set_visible(list, FALSE);
			spin(50);
			gtk_widget_set_visible(list, TRUE);
			spin(50);
		}
		spin(300);
		CHECK_LOG(0, "] relook\n");
		CHECK_FIRST("Main-0");
		break;
	}

	case 33: { /* lifetime */
		GtkWidget *stack, *pages[2];
		/* a rebuild pending at a switch: cancelled, one sync */
		GMenuModel *m1 = menu_n("P1", 3, 0);
		pages[0] = page_with_menu(m1, TRUE, NULL);
		pages[1] = page_with_menu(menu_n("P2", 5, 0), TRUE, NULL);
		window = stack_window(&stack, pages, 2);
		gtk_window_present(GTK_WINDOW(window));
		wait_filled();
		spin(300);
		show_page(stack, "p2"); /* unmaps page 1: the relook idle first */
		relabel(G_MENU(m1), 0, "P1-changed", "app.new");
		wait_log_n("switched to the shown menu", 1);
		spin(300);
		CHECK_LOG(0, "menu changed (rebuild 1)");
		CHECK_FIRST("P2-0");
		/* the application takes the menubar: no relook afterwards */
		GMenuModel *own = main_menu();
		gtk_application_set_menubar(app, own);
		spin(200);
		int relooks = log_count("] relook\n");
		show_page(stack, "p1");
		spin(500);
		CHECK(log_count("] relook\n") == relooks, "a relook after TAKEN");
		g_object_unref(own);
		break;
	}

	case 35: { /* a window destroyed and the application quit with a relook pending */
		GtkWidget *stack, *pages[2];
		pages[0] = page_with_menu(menu_n("P1", 3, 0), TRUE, NULL);
		pages[1] = page_with_menu(menu_n("P2", 5, 0), TRUE, NULL);
		window = stack_window(&stack, pages, 2);
		gtk_window_present(GTK_WINDOW(window));
		wait_filled();
		spin(300);
		show_page(stack, "p2"); /* schedules a relook */
		gtk_window_destroy(GTK_WINDOW(window));
		break; /* the application quits in this turn */
	}

	case 34: { /* B: relabels look at no other window; a new action does */
		GMenu *s = g_menu_new();
		g_menu_append(s, "One", "app.new");
		GMenu *top = g_menu_new();
		g_menu_append_section(top, NULL, G_MENU_MODEL(s));
		for (int k = 0; k < 10; k++)
			filled_window(G_MENU_MODEL(top), NULL);
		spin(300);
		int looks = log_count("chose candidate");
		for (int i = 0; i < 20; i++) {
			char *l = g_strdup_printf("One %d", i);
			relabel(s, 0, l, "app.new");
			g_free(l);
			wait_rebuild(i + 1);
		}
		if (!control)
			CHECK(log_count("chose candidate") == looks,
			      "%d window looks for 20 relabels",
			      log_count("chose candidate") - looks);
		g_menu_append(s, "Quit", "app.quit");
		wait_rebuild(21);
		if (!control)
			CHECK(log_count("chose candidate") == looks + 9,
			      "%d window looks for a new action, not 9",
			      log_count("chose candidate") - looks);
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
	/* TRUE as under Unity; LATETEST_SHELL_MENUBAR=0 for the run without a
	   shell menubar, where GTK puts an in-window bar on our menubar */
	const char *shell = g_getenv("LATETEST_SHELL_MENUBAR");
	g_object_set(gtk_settings_get_default(), "gtk-shell-shows-menubar",
		     shell == NULL || strcmp(shell, "0") != 0, NULL);
	g_application_hold(a);
	run_case();
	g_application_release(a);
}

int main(int argc, char **argv)
{
	which = argc > 1 ? atoi(argv[1]) : 1;
	control = g_getenv("LATETEST_CONTROL") != NULL;

	char *id = g_strdup_printf("org.unitydistro.LateTest%d", which);
	app = gtk_application_new(id, G_APPLICATION_NON_UNIQUE);
	g_free(id);
	g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
	g_application_run(G_APPLICATION(app), 1, argv);
	g_object_unref(app);

	if (which == 18 && kept != NULL) {
		relabel(G_MENU(kept), 0, "After the application", "app.new");
		spin(300);
		CHECK_LOG(0, "menu changed");
		g_object_unref(kept);
	}

	printf("%s case %d%s\n", failed ? "FAIL" : "PASS", which,
	       control ? " (control: library log not checked)" : "");
	return failed;
}
