#!/usr/bin/env python3
"""Small native file-drop window for installing IPAs into ios-kvm."""
import threading
from urllib.parse import urlparse, unquote
import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, Gdk, GLib
from ipa_install import install, InstallError


class Window(Gtk.Window):
    def __init__(self):
        super().__init__(title='ios-kvm · Install IPA')
        self.set_default_size(440, 230)
        self.set_border_width(24)
        self.busy = False
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=16)
        self.add(box)
        heading = Gtk.Label(label='Drop an IPA here')
        heading.get_style_context().add_class('title')
        box.pack_start(heading, True, True, 0)
        self.message = Gtk.Label(label='Installs into your running ios-kvm device.')
        self.message.set_line_wrap(True)
        self.message.set_max_width_chars(52)
        box.pack_start(self.message, False, False, 0)
        self.progress = Gtk.Spinner()
        box.pack_start(self.progress, False, False, 0)
        self.choose = Gtk.Button(label='Choose IPA…')
        self.choose.connect('clicked', self.pick)
        box.pack_start(self.choose, False, False, 0)
        self.drag_dest_set(Gtk.DestDefaults.ALL, [Gtk.TargetEntry.new('text/uri-list', 0, 0)], Gdk.DragAction.COPY)
        self.connect('drag-data-received', self.drop)
        self.connect('delete-event', lambda *_: self.busy)
        self.connect('destroy', Gtk.main_quit)

    def pick(self, *_):
        dialog = Gtk.FileChooserDialog(title='Choose an IPA', parent=self,
            action=Gtk.FileChooserAction.OPEN)
        dialog.add_buttons('Cancel', Gtk.ResponseType.CANCEL, 'Install', Gtk.ResponseType.OK)
        pattern = Gtk.FileFilter(); pattern.set_name('iOS apps (*.ipa)'); pattern.add_pattern('*.ipa'); pattern.add_pattern('*.IPA')
        dialog.add_filter(pattern)
        if dialog.run() == Gtk.ResponseType.OK:
            self.begin(dialog.get_filename())
        dialog.destroy()

    def drop(self, _widget, context, _x, _y, selection, _info, timestamp):
        uris = selection.get_uris() or []
        accepted = False
        if not self.busy and len(uris) == 1:
            uri = urlparse(uris[0])
            if uri.scheme == 'file' and uri.netloc in ('', 'localhost'):
                self.begin(unquote(uri.path)); accepted = True
        if not accepted:
            self.message.set_text('Drop one local IPA at a time; wait for the current installation to finish.')
        Gtk.drag_finish(context, accepted, False, timestamp)

    def begin(self, path):
        if self.busy:
            return
        self.busy = True
        self.choose.set_sensitive(False)
        self.progress.start()
        threading.Thread(target=self.work, args=(path,), daemon=True).start()

    def update(self, text):
        self.message.set_text(text)
        return False

    def work(self, path):
        try:
            install(path, lambda text: GLib.idle_add(self.update, text))
        except InstallError as error:
            GLib.idle_add(self.update, str(error))
        finally:
            GLib.idle_add(self.finished)

    def finished(self):
        self.busy = False
        self.choose.set_sensitive(True)
        self.progress.stop()
        return False


if __name__ == '__main__':
    Window().show_all()
    Gtk.main()
