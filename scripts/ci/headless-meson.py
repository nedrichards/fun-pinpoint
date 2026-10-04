"""Run Meson with private, test-only desktop services.

Headless CI lacks desktop services. Inhibition and session registration are
in-memory fixtures, never host inhibitors. No camera/file portal is provided.
Real portal and inhibition checks remain installed-app release gates.
"""

import os
import subprocess
import sys

import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib  # noqa: E402

XML = """<node><interface name="org.freedesktop.portal.Flatpak">
<property name="version" type="u" access="read"/>
<property name="supports" type="u" access="read"/>
</interface></node>"""


DESKTOP_XML = """<node><interface name="org.freedesktop.portal.Inhibit">
<property name="version" type="u" access="read"/>
<method name="Inhibit"><arg type="s" direction="in"/><arg type="u" direction="in"/>
<arg type="a{sv}" direction="in"/><arg type="o" direction="out"/></method>
<method name="CreateMonitor"><arg type="s" direction="in"/>
<arg type="a{sv}" direction="in"/><arg type="o" direction="out"/></method>
<method name="QueryEndResponse"><arg type="o" direction="in"/></method>
</interface></node>"""
CLOSE_XML = """<node><interface name="org.freedesktop.portal.Request">
<method name="Close"/></interface><interface name="org.freedesktop.portal.Session">
<method name="Close"/></interface></node>"""


SESSION_XML = """<node><interface name="org.gnome.SessionManager">
<method name="Inhibit"><arg type="s" direction="in"/><arg type="u" direction="in"/>
<arg type="s" direction="in"/><arg type="u" direction="in"/><arg type="u" direction="out"/></method>
<method name="Uninhibit"><arg type="u" direction="in"/></method>
<method name="IsInhibited"><arg type="u" direction="in"/><arg type="b" direction="out"/></method>
<method name="RegisterClient"><arg type="s" direction="in"/><arg type="s" direction="in"/>
<arg type="o" direction="out"/></method>
<method name="UnregisterClient"><arg type="o" direction="in"/></method>
</interface></node>"""


def main():
    args = sys.argv[1:]
    if not args:
        raise SystemExit("usage: headless-meson.py COMMAND [ARGUMENT...]")
    os.environ.pop("GDK_DEBUG", None)
    os.environ["GSETTINGS_BACKEND"] = "memory"
    loop = GLib.MainLoop()
    process = None
    status = 1
    registered = []
    available = set()
    cookies = set()
    next_cookie = 1
    next_client = 1
    clients = {}

    def session_call(
        _connection, _sender, _path, _interface, method, parameters, invocation
    ):
        nonlocal next_cookie, next_client
        if method == "Inhibit":
            cookie = next_cookie
            next_cookie += 1
            cookies.add(cookie)
            invocation.return_value(GLib.Variant("(u)", (cookie,)))
        elif method == "Uninhibit":
            cookies.discard(parameters.unpack()[0])
            invocation.return_value(GLib.Variant("()", ()))
        elif method == "IsInhibited":
            invocation.return_value(GLib.Variant("(b)", (bool(cookies),)))
        elif method == "RegisterClient":
            path = f"/org/gnome/SessionManager/Client{next_client}"
            next_client += 1
            interface = Gio.DBusNodeInfo.new_for_xml(
                '<node><interface name="org.gnome.SessionManager.ClientPrivate">'
                '<method name="EndSessionResponse"><arg type="b" direction="in"/>'
                '<arg type="s" direction="in"/></method></interface></node>'
            ).interfaces[0]
            clients[path] = _connection.register_object(
                path, interface, session_call, None, None
            )
            invocation.return_value(GLib.Variant("(o)", (path,)))
        elif method == "UnregisterClient":
            client = clients.pop(parameters.unpack()[0], None)
            if client is not None:
                _connection.unregister_object(client)
            invocation.return_value(GLib.Variant("()", ()))
        elif method == "EndSessionResponse":
            invocation.return_value(GLib.Variant("()", ()))
        else:
            invocation.return_dbus_error(
                "org.freedesktop.DBus.Error.UnknownMethod", method
            )

    def portal_call(
        connection, sender, _path, _interface, method, parameters, invocation
    ):
        nonlocal next_cookie
        if method in ("Close", "QueryEndResponse"):
            invocation.return_value(GLib.Variant("()", ()))
            return
        values = parameters.unpack()
        token = values[-1].get("handle_token", f"fixture{next_cookie}")
        next_cookie += 1
        sender_path = sender.lstrip(":").replace(".", "_")
        path = f"/org/freedesktop/portal/desktop/request/{sender_path}/{token}"
        registered.extend(
            connection.register_object(path, interface, portal_call, None, None)
            for interface in Gio.DBusNodeInfo.new_for_xml(CLOSE_XML).interfaces
        )
        invocation.return_value(GLib.Variant("(o)", (path,)))
        if method == "CreateMonitor":
            session_token = values[-1]["session_handle_token"]
            session = (
                f"/org/freedesktop/portal/desktop/session/{sender_path}/{session_token}"
            )
            interface = Gio.DBusNodeInfo.new_for_xml(CLOSE_XML).interfaces[1]
            registered.append(
                connection.register_object(session, interface, portal_call, None, None)
            )

            def respond():
                connection.emit_signal(
                    sender,
                    path,
                    "org.freedesktop.portal.Request",
                    "Response",
                    GLib.Variant(
                        "(ua{sv})", (0, {"session_handle": GLib.Variant("o", session)})
                    ),
                )
                return False

            GLib.idle_add(respond)

    def acquired(connection, name):
        if name == "org.freedesktop.portal.Flatpak":
            interface = Gio.DBusNodeInfo.new_for_xml(XML).interfaces[0]
            registered.append(
                connection.register_object(
                    "/org/freedesktop/portal/Flatpak",
                    interface,
                    None,
                    lambda *_args: GLib.Variant("u", 0),
                    None,
                )
            )
        elif name == "org.freedesktop.portal.Desktop":
            interface = Gio.DBusNodeInfo.new_for_xml(DESKTOP_XML).interfaces[0]
            registered.append(
                connection.register_object(
                    "/org/freedesktop/portal/desktop",
                    interface,
                    portal_call,
                    lambda *_args: GLib.Variant("u", 3),
                    None,
                )
            )
        else:
            interface = Gio.DBusNodeInfo.new_for_xml(SESSION_XML).interfaces[0]
            registered.append(
                connection.register_object(
                    "/org/gnome/SessionManager", interface, session_call, None, None
                )
            )

    def ready(_connection, name):
        nonlocal process
        available.add(name)
        if len(available) == 3 and process is None:
            process = subprocess.Popen(args)  # noqa: S603 -- argv supplied by the fixed CI test command
            GLib.timeout_add(50, poll)

    def poll():
        nonlocal status
        code = process.poll()
        if code is None:
            return True
        status = code if code >= 0 else 128 - code
        loop.quit()
        return False

    def lost(_connection, _name):
        if process is not None and process.poll() is None:
            process.terminate()
        loop.quit()

    owners = [
        Gio.bus_own_name(
            Gio.BusType.SESSION, name, Gio.BusNameOwnerFlags.NONE, acquired, ready, lost
        )
        for name in (
            "org.freedesktop.portal.Flatpak",
            "org.freedesktop.portal.Desktop",
            "org.gnome.SessionManager",
        )
    ]
    try:
        loop.run()
    finally:
        for owner in owners:
            Gio.bus_unown_name(owner)
    return status


if __name__ == "__main__":
    raise SystemExit(main())
