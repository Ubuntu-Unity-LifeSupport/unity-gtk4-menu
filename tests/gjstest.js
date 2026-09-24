// gjstest - classtest for gjs: a class action on the window, disabled in
// init, and an exported action that re-enables it with action_set_enabled.
import Gio from 'gi://Gio';
import GObject from 'gi://GObject';
import Gtk from 'gi://Gtk?version=4.0';

const TestWindow = GObject.registerClass(class TestWindow extends Gtk.ApplicationWindow {
    static {
        this.install_action('win.gjs-hello', null, () => print('ACTIVATED win.gjs-hello'));
    }

    constructor(app) {
        super({application: app, title: 'gjstest'});
        const toggle = new Gio.SimpleAction({name: 'enable-hello'});
        toggle.connect('activate', () => {
            this.action_set_enabled('win.gjs-hello', true);
            print('GJS-HELLO enabled');
        });
        this.add_action(toggle);
        this.action_set_enabled('win.gjs-hello', false);

        const menu = new Gio.Menu();
        menu.append('Gjs hello', 'win.gjs-hello');
        const button = new Gtk.MenuButton({menu_model: menu, primary: true});
        const header = new Gtk.HeaderBar();
        header.pack_end(button);
        this.set_titlebar(header);
    }
});

const app = new Gtk.Application({application_id: 'org.unitydistro.GjsTest'});
app.connect('activate', () => new TestWindow(app).present());
app.run([]);
