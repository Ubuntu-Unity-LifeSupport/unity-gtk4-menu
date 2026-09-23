/*
 * classtest - a header bar menu whose items name actions the window does not
 * export over D-Bus, the way yelp's "About Help" does.
 *
 *   win.class-hello   class action on the window class      -> should be proxied
 *   win.class-param   class action with a string parameter -> should be proxied
 *   win.class-toggle  property action (boolean property)   -> left alone
 *   inner.hello       group inserted on a sub-widget       -> left alone
 *   win.map-hello     ordinary entry in the window's map   -> exported as is
 *
 * Every activation prints one line to stdout, so a test can activate over
 * D-Bus and read what happened.
 *
 *   cc -o classtest tests/classtest.c $(pkg-config --cflags --libs gtk4)
 */
#include <gtk/gtk.h>

#define TEST_TYPE_WINDOW (test_window_get_type())
G_DECLARE_FINAL_TYPE(TestWindow, test_window, TEST, WINDOW, GtkApplicationWindow)

struct _TestWindow {
	GtkApplicationWindow parent;
	gboolean toggle;
};

G_DEFINE_TYPE(TestWindow, test_window, GTK_TYPE_APPLICATION_WINDOW)

enum { PROP_TOGGLE = 1 };

static void say(const char *what)
{
	g_print("ACTIVATED %s\n", what);
}

static void class_hello(GtkWidget *w, const char *name, GVariant *p)
{
	say(name);
}

static void class_param(GtkWidget *w, const char *name, GVariant *p)
{
	g_print("ACTIVATED %s %s\n", name, g_variant_get_string(p, NULL));
}

static void map_hello(GSimpleAction *a, GVariant *p, gpointer d)
{
	say("win.map-hello");
}

static void inner_hello(GSimpleAction *a, GVariant *p, gpointer d)
{
	say("inner.hello");
}

static void set_property(GObject *o, guint id, const GValue *v, GParamSpec *ps)
{
	TEST_WINDOW(o)->toggle = g_value_get_boolean(v);
	g_print("TOGGLE %d\n", TEST_WINDOW(o)->toggle);
}

static void get_property(GObject *o, guint id, GValue *v, GParamSpec *ps)
{
	g_value_set_boolean(v, TEST_WINDOW(o)->toggle);
}

static void test_window_class_init(TestWindowClass *klass)
{
	GObjectClass *oc = G_OBJECT_CLASS(klass);
	GtkWidgetClass *wc = GTK_WIDGET_CLASS(klass);

	oc->set_property = set_property;
	oc->get_property = get_property;
	g_object_class_install_property(
		oc, PROP_TOGGLE,
		g_param_spec_boolean("toggle", NULL, NULL, FALSE,
				     G_PARAM_READWRITE));

	gtk_widget_class_install_action(wc, "win.class-hello", NULL, class_hello);
	gtk_widget_class_install_action(wc, "win.class-param", "s", class_param);
	gtk_widget_class_install_property_action(wc, "win.class-toggle", "toggle");
}

static void test_window_init(TestWindow *self)
{
	static const GActionEntry entries[] = {
		{ .name = "map-hello", .activate = map_hello },
	};
	g_action_map_add_action_entries(G_ACTION_MAP(self), entries, 1, self);

	GMenu *menu = g_menu_new();
	g_menu_append(menu, "Class hello", "win.class-hello");
	g_menu_append(menu, "Class param", "win.class-param::abc");
	g_menu_append(menu, "Class toggle", "win.class-toggle");
	g_menu_append(menu, "Inner hello", "inner.hello");
	g_menu_append(menu, "Map hello", "win.map-hello");

	GtkWidget *button = gtk_menu_button_new();
	gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(button), G_MENU_MODEL(menu));
	g_object_unref(menu);

	GtkWidget *header = gtk_header_bar_new();
	gtk_header_bar_pack_end(GTK_HEADER_BAR(header), button);
	gtk_window_set_titlebar(GTK_WINDOW(self), header);

	GSimpleActionGroup *inner = g_simple_action_group_new();
	static const GActionEntry inner_entries[] = {
		{ .name = "hello", .activate = inner_hello },
	};
	g_action_map_add_action_entries(G_ACTION_MAP(inner), inner_entries, 1, NULL);
	gtk_widget_insert_action_group(header, "inner", G_ACTION_GROUP(inner));
	g_object_unref(inner);

	gtk_window_set_title(GTK_WINDOW(self), "classtest");
	gtk_window_set_default_size(GTK_WINDOW(self), 400, 200);
}

static void activate(GApplication *app)
{
	GtkWindow *w = g_object_new(TEST_TYPE_WINDOW, "application", app, NULL);
	gtk_window_present(w);
}

int main(int argc, char **argv)
{
	GtkApplication *app = gtk_application_new("org.unitydistro.ClassTest",
						  G_APPLICATION_DEFAULT_FLAGS);
	g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
	return g_application_run(G_APPLICATION(app), argc, argv);
}
