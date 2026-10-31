"""Stand-ins for a desktop's tray host and notification server (stage 5 test).

  python3 -I fake_desktop.py watcher|notifications

Owns org.kde.StatusNotifierWatcher or org.freedesktop.Notifications on the session
bus and prints one JSON line per call it receives. Nothing is shown on screen.
"""
import json
import sys

import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib  # noqa: E402

WATCHER = ("org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher", """<node>
<interface name="org.kde.StatusNotifierWatcher">
  <method name="RegisterStatusNotifierItem"><arg type="s" direction="in"/></method>
  <method name="RegisterStatusNotifierHost"><arg type="s" direction="in"/></method>
  <property name="RegisteredStatusNotifierItems" type="as" access="read"/>
  <property name="IsStatusNotifierHostRegistered" type="b" access="read"/>
  <property name="ProtocolVersion" type="i" access="read"/>
  <signal name="StatusNotifierItemRegistered"><arg type="s"/></signal>
</interface></node>""")

NOTIFICATIONS = ("org.freedesktop.Notifications", "/org/freedesktop/Notifications", """<node>
<interface name="org.freedesktop.Notifications">
  <method name="Notify">
    <arg type="s" direction="in"/><arg type="u" direction="in"/><arg type="s" direction="in"/>
    <arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="as" direction="in"/>
    <arg type="a{sv}" direction="in"/><arg type="i" direction="in"/><arg type="u" direction="out"/>
  </method>
  <method name="CloseNotification"><arg type="u" direction="in"/></method>
  <method name="GetCapabilities"><arg type="as" direction="out"/></method>
  <method name="GetServerInformation">
    <arg type="s" direction="out"/><arg type="s" direction="out"/><arg type="s" direction="out"/><arg type="s" direction="out"/>
  </method>
</interface></node>""")

name, path, xml = WATCHER if sys.argv[1] == "watcher" else NOTIFICATIONS
interface = Gio.DBusNodeInfo.new_for_xml(xml).interfaces[0]
items, notices = [], [0]


def emit(event):
    print(json.dumps(event, ensure_ascii=False), flush=True)


def on_call(connection, sender, object_path, interface_name, method, parameters, invocation):
    args = parameters.unpack()
    if method == "RegisterStatusNotifierItem":
        items.append(args[0])
        emit({"call": method, "service": args[0], "sender": sender})
        invocation.return_value(None)
        connection.emit_signal(None, object_path, interface_name, "StatusNotifierItemRegistered", GLib.Variant("(s)", (args[0],)))
    elif method == "Notify":
        notices[0] += 1
        emit({"call": method, "app": args[0], "replaces": args[1], "icon": args[2], "title": args[3], "body": args[4],
              "hints": dict(args[6]), "timeout": args[7]})
        invocation.return_value(GLib.Variant("(u)", (notices[0],)))
    elif method == "GetCapabilities":
        invocation.return_value(GLib.Variant("(as)", (["body"],)))
    elif method == "GetServerInformation":
        invocation.return_value(GLib.Variant("(ssss)", ("fake", "sukut-test", "1", "1.2")))
    else:
        emit({"call": method})
        invocation.return_value(None)


def on_property(connection, sender, object_path, interface_name, prop):
    return {"RegisteredStatusNotifierItems": GLib.Variant("as", items),
            "IsStatusNotifierHostRegistered": GLib.Variant("b", True),
            "ProtocolVersion": GLib.Variant("i", 0)}[prop]


def on_bus(connection, _name):
    connection.register_object(path, interface, on_call, on_property, None)


Gio.bus_own_name(Gio.BusType.SESSION, name, Gio.BusNameOwnerFlags.NONE, on_bus,
                 lambda *_: emit({"ready": name}), lambda *_: emit({"lost": name}))
GLib.MainLoop().run()
