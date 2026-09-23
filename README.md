# unity-gtk4-menu

Exports a GTK4 application's header bar menu to the Unity global menu and the
HUD.

GTK4 applications keep their menu in a `GtkMenuButton` in the header bar rather
than in a menu bar, so Unity's panel has nothing to show for them. GTK4 also
removed the module loading mechanism GTK3 has, which is how `appmenu-gtk-module`
reaches GTK3 applications, so there is no module to write.

This is an `LD_PRELOAD` library instead. It overwrites `realize` in the
`GtkWindow` class vtable, finds the header bar's menu model through public API,
and sets it as the application's menu bar before the window is realized. GTK4
then exports it over `org.gtk.Menus` and advertises it on the window, which is
all Unity needs - the transport was never missing, only a producer.

The same approach is already shipped in Ubuntu Unity by `gtk-nocsd`, which
disables client side decorations the same way, and this package follows its
packaging closely.

## Configuration

    gsettings set com.ubuntu-unity.gtk4-menu show-application-name false

A header bar menu has no name of its own. With this on, the entry is named
after the application, which repeats the name the panel shows beside it; with
it off, a neutral label is used. Default is on.

To disable the library entirely, shadow its environment snippet with an empty
file of the same name:

    # everyone
    sudo touch /etc/environment.d/60-unity-gtk4-menu.conf
    # one user
    touch ~/.config/environment.d/60-unity-gtk4-menu.conf

## Actions the window does not export

The global menu can only activate what the application exports over D-Bus: the
application's actions (`app.`) and a `GtkApplicationWindow`'s own action map
(`win.`). Header bar menus may also name actions installed on a widget class
with `gtk_widget_class_install_action()` - yelp's "About Help" is one. Inside
the application the popover resolves them through the widget's action muxer;
over D-Bus nobody can, so they used to arrive greyed out.

For each such item the exported copy of the menu points at a stand-in added to
the window's action map, named `unity-gtk4-menu-<original name>`, which
activates the original from the menu button. Known limits:

- A stand-in is always shown enabled. GTK has no public getter for a class
  action's enabled state; activating a disabled one does nothing, as it would
  in the application.
- Property actions (`gtk_widget_class_install_property_action()`) are left
  alone: they carry state a plain stand-in cannot mirror.
- Actions with a prefix other than `app.` or `win.` usually come from a group
  inserted on a sub-widget with `gtk_widget_insert_action_group()`, which
  public API cannot enumerate. They are left alone too.

The debug log names every action in each of these cases, so a new application
shows at once which one it hits.

## Testing without installing

    make
    LD_PRELOAD=./libunity-gtk4-menu.so.0 UNITY_GTK4_MENU_DEBUG=1 \
      UNITY_GTK4_MENU_LOG=/tmp/shim.log some-gtk4-application

`UNITY_GTK4_MENU_LABEL=app|generic` overrides the setting.
`UNITY_GTK4_MENU_FORCE=1` skips the check that the session is Unity.

`tests/classtest.c` is a minimal application with one item of each kind above;
every activation prints a line, so a test can activate the exported actions
with `gdbus call ... org.gtk.Actions.Activate` and read the result.
