#!/usr/bin/env python3
"""A scaled local VM display with QMP absolute-touch input.

RFB carries pixels only; pointer coordinates are mapped to the complete iPhone
framebuffer independently of window size. Closing this window leaves the VM up.
"""
import json
import socket
import time
import concurrent.futures
import gi

gi.require_version('Gtk', '3.0')
gi.require_version('GtkVnc', '2.0')
gi.require_version('Gdk', '3.0')
from gi.repository import Gtk, GtkVnc, Gdk


class Monitor:
    def __init__(self):
        self.command('query-status')

    def command(self, command, **arguments):
        # Release QMP after each transaction so diagnostic tools can use it too.
        with socket.create_connection(('127.0.0.1', 4445), timeout=2) as sock:
            with sock.makefile('rw') as file:
                file.readline()
                for name, args in [('qmp_capabilities', {}), (command, arguments)]:
                    file.write(json.dumps({'execute': name, 'arguments': args}) + '\n')
                    file.flush()
                    while True:
                        response = json.loads(file.readline())
                        if 'error' in response:
                            raise RuntimeError(response['error']['desc'])
                        if 'return' in response:
                            break
            return response['return']

    def touch(self, x, y, down=None):
        events = [{'type': 'abs', 'data': {'axis': axis, 'value': round(max(0, min(1, value)) * 32767)}}
                  for axis, value in [('x', x), ('y', y)]]
        if down is not None:
            events.append({'type': 'btn', 'data': {'button': 'left', 'down': down}})
        self.command('input-send-event', events=events)

    def key(self, key):
        self.command('send-key', keys=[{'type': 'qcode', 'data': key}], **{'hold-time': 100})


class Viewer:
    def __init__(self):
        self.monitor = Monitor()
        self.worker = concurrent.futures.ThreadPoolExecutor(max_workers=1)
        self.pending = None
        self.dragging = False
        self.last_motion = 0
        self.window = Gtk.Window(title='ios-kvm — iPhone X / iOS 16.7.16')
        monitor = Gdk.Display.get_default().get_primary_monitor()
        if monitor is None:
            monitor = Gdk.Display.get_default().get_monitor(0)
        area = monitor.get_workarea()
        height = min(900, area.height - 160)
        self.window.set_default_size(round(height * 1125 / 2436), height)
        self.window.connect('destroy', Gtk.main_quit)
        header = Gtk.HeaderBar(title='ios-kvm', subtitle='iPhone X · iOS 16.7.16')
        header.set_show_close_button(True)
        for title, key in [('Home', 'h'), ('Power', 'l')]:
            button = Gtk.Button(label=title)
            button.connect('clicked', lambda _button, k=key: self.worker.submit(self.monitor.key, k))
            header.pack_start(button)
        self.window.set_titlebar(header)
        aspect = Gtk.AspectFrame(xalign=0.5, yalign=0.5, ratio=1125 / 2436, obey_child=False)
        aspect.set_shadow_type(Gtk.ShadowType.NONE)
        self.input = Gtk.EventBox()
        self.input.set_above_child(True)
        self.input.add_events(Gdk.EventMask.BUTTON_PRESS_MASK | Gdk.EventMask.BUTTON_RELEASE_MASK |
                              Gdk.EventMask.POINTER_MOTION_MASK)
        self.input.connect('button-press-event', self.press)
        self.input.connect('button-release-event', self.release)
        self.input.connect('motion-notify-event', self.motion)
        self.display = GtkVnc.Display()
        self.display.set_scaling(True)
        self.display.set_keep_aspect_ratio(False)  # AspectFrame provides the exact ratio.
        self.display.set_allow_resize(False)
        self.display.set_force_size(False)
        self.display.set_size_request(1, 1)
        self.display.set_read_only(True)
        self.input.add(self.display)
        aspect.add(self.input)
        self.window.add(aspect)
        self.window.show_all()
        self.display.open_host('127.0.0.1', '5901')

    def point(self, event):
        size = self.input.get_allocation()
        return event.x / max(1, size.width), event.y / max(1, size.height)

    def press(self, _widget, event):
        if event.button == 1:
            self.dragging = True
            self.pending = self.worker.submit(self.monitor.touch, *self.point(event), down=True)
        return True

    def release(self, _widget, event):
        if event.button == 1 and self.dragging:
            self.pending = self.worker.submit(self.monitor.touch, *self.point(event), down=False)
            self.dragging = False
        return True

    def motion(self, _widget, event):
        now = time.monotonic()
        if self.dragging and now - self.last_motion > 0.02 and (self.pending is None or self.pending.done()):
            self.pending = self.worker.submit(self.monitor.touch, *self.point(event))
            self.last_motion = now
        return True


if __name__ == '__main__':
    Viewer()
    Gtk.main()
