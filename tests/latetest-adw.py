#!/usr/bin/python3
"""latetest-adw.py CASE - the libadwaita cases of latetest (unity-gtk4-menu 0.12).

  6   while an AdwApplicationWindow waits for a menu, an AdwDialog with a menu
      button in its own AdwHeaderBar is presented inside it, and a menu button
      inside a GtkPopover is shown: each makes a late look, and neither becomes
      the application's menubar.
  6a  Pinta's layout: an AdwApplicationWindow with an AdwToolbarView and an
      AdwHeaderBar in the content; the menu buttons are packed after present,
      libadwaita being loaded through GI after GTK. The main menu becomes the
      menubar.

Not part of `make check`. Run on a test machine with gir1.2-adw-1, the library
preloaded and UNITY_GTK4_MENU_FORCE=1, under dbus-run-session and xvfb-run,
as tests/latetest.c describes. LATETEST_CONTROL=1 skips the library log
checks. Exit status 0 = PASS.
"""
import os
import sys

import gi
gi.require_version('Gtk', '4.0')
from gi.repository import Gio, GLib, Gtk  # noqa: E402  GTK first, as Pinta's order
gi.require_version('Adw', '1')
from gi.repository import Adw  # noqa: E402

case = sys.argv[1] if len(sys.argv) > 1 else '6a'
control = 'LATETEST_CONTROL' in os.environ
failures = []


def check(cond, text):
    if not cond:
        failures.append(text)
        print(f'FAIL case {case}: {text}', file=sys.stderr)


def log_has(text):
    path = os.environ.get('UNITY_GTK4_MENU_LOG')
    try:
        return path is not None and text in open(path).read()
    except OSError:
        return False


def check_log(present, text):
    if not control:
        check(log_has(text) == present, f'log line "{text}" expected {"present" if present else "absent"}')


def log_count(text):
    path = os.environ.get('UNITY_GTK4_MENU_LOG')
    try:
        return 0 if path is None else open(path).read().count(text)
    except OSError:
        return 0


def wait_log_count(text, n):
    """Wait up to 3 s until TEXT is in the library's log at least N times."""
    for _ in range(150):
        if log_count(text) >= n:
            return
        spin(20)


def wait_filled(app):
    """Wait up to 3 s for a filled holder in the application's menubar."""
    for _ in range(150):
        if holder_items(app.get_menubar()) > 0:
            return
        spin(20)


def spin(ms):
    end = GLib.get_monotonic_time() + ms * 1000
    ctx = GLib.MainContext.default()
    while GLib.get_monotonic_time() < end:
        ctx.iteration(False)


def holder_items(menubar):
    if menubar is None or menubar.get_n_items() == 0:
        return 0
    sub = menubar.get_item_link(0, 'submenu')
    return -1 if sub is None else sub.get_n_items()


def menu(label, actions):
    top = Gio.Menu()
    sub = Gio.Menu()
    for name in actions:
        sub.append(name.title(), 'app.' + name)
    top.append_submenu(label, sub)
    return top


def run(app):
    for name in ('new', 'open', 'quit', 'about'):
        action = Gio.SimpleAction.new(name, None)
        app.add_action(action)

    Gtk.Settings.get_default().set_property('gtk-shell-shows-menubar', True)  # as under Unity
    win = Adw.ApplicationWindow(application=app)
    view = Adw.ToolbarView()
    header = Adw.HeaderBar()
    view.add_top_bar(header)
    view.set_content(Gtk.Box())
    win.set_content(view)
    win.present()
    spin(300)

    if case == '6':
        # a realistic dialog: its menu button in its own AdwHeaderBar, so only the
        # AdwDialog exclusion keeps it out (not the header-bar rule)
        looks = log_count('late look')
        dialog = Adw.Dialog()
        dview = Adw.ToolbarView()
        dheader = Adw.HeaderBar()
        dbutton = Gtk.MenuButton(menu_model=menu('_Dialog', ['new', 'open']))
        dheader.pack_end(dbutton)
        dview.add_top_bar(dheader)
        dview.set_content(Gtk.Box())
        dialog.set_child(dview)
        dialog.present(win)
        for _ in range(150):
            if dbutton.get_mapped():
                break
            spin(20)
        if dbutton.get_root() is win:
            # a sheet inside the window: the map makes a look, the AdwDialog exclusion rejects it
            print(f'case {case}: the dialog is a sheet in the window', flush=True)
            wait_log_count('late look', looks + 1)
            spin(200)
            if not control:
                check(log_count('late look') > looks, "the dialog's menu button made no late look")
        else:
            # gtk-nocsd makes the dialog a window of its own: not the waiting window, no look
            print(f'case {case}: the dialog is a window of its own ({type(dbutton.get_root()).__name__})', flush=True)
            spin(500)
            if not control:
                check(log_count('late look') == looks, "a look ran for a dialog that is a window of its own")
        check(holder_items(app.get_menubar()) == 0, "a dialog's menu became the menubar")
        popover = Gtk.Popover()
        popover.set_child(Gtk.MenuButton(menu_model=menu('_Pop', ['quit'])))
        popover.set_parent(header)
        looks = log_count('late look')
        popover.popup()
        wait_log_count('late look', looks + 1)
        spin(200)
        if not control:
            check(log_count('late look') > looks, "the popover's menu button made no late look")
        check(holder_items(app.get_menubar()) == 0, "a popover's menu became the menubar")
        check_log(True, 'the window waits for a menu')
        popover.unparent()
    else:
        # Pinta packs Effects, Adjustments and the main menu after present
        header.pack_end(Gtk.MenuButton(menu_model=menu('_Effects', ['about'])))
        header.pack_end(Gtk.MenuButton(menu_model=menu('_File', ['new', 'open', 'quit'])))
        wait_filled(app)
        check(holder_items(app.get_menubar()) > 0, 'the late header-bar menu is not in the menubar')
        check_log(True, 'menubar filled late')
    app.quit()


app = Adw.Application(application_id=f'org.unitydistro.LateTestAdw{case}',
                      flags=Gio.ApplicationFlags.NON_UNIQUE)
app.connect('activate', run)
app.run([sys.argv[0]])
print(('FAIL' if failures else 'PASS') + f' case {case}' + (' (control: library log not checked)' if control else ''))
sys.exit(1 if failures else 0)
