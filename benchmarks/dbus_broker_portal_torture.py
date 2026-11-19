#!/usr/bin/env python3
"""
Real-world /usr/bin/dbus-broker + libgio GDBus xdg-desktop-portal torture test.
Spawns a private /usr/bin/dbus-broker-launch instance on an AF_UNIX socket,
registers a portal-like GDBus service returning >256B Settings/Registry/Inhibit
payloads mixed with SCM_RIGHTS UnixFDList descriptors and back-to-back signals,
and hammers it from 6 concurrent GDBus client processes.
"""
import os
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
from multiprocessing import Process, Queue

import gi
gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib

IFACE_XML = """
<node>
  <interface name="org.netzero.PortalTorture">
    <method name="OpenPipeAndSettings">
      <arg type="s" name="app_id" direction="in"/>
      <arg type="h" name="fd_idx" direction="out"/>
      <arg type="a{sv}" name="settings" direction="out"/>
    </method>
    <method name="ReadLargeRegistry">
      <arg type="u" name="seq" direction="in"/>
      <arg type="s" name="blob" direction="out"/>
    </method>
    <signal name="SettingChanged">
      <arg type="s" name="ns"/>
      <arg type="s" name="key"/>
      <arg type="s" name="val"/>
    </signal>
  </interface>
</node>
"""


def run_portal_service(bus_addr: str, ready_q: Queue):
    conn = Gio.DBusConnection.new_for_address_sync(
        bus_addr,
        Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT
        | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,
        None,
        None,
    )
    node_info = Gio.DBusNodeInfo.new_for_xml(IFACE_XML)
    iface_info = node_info.interfaces[0]

    def on_method_call(connection, sender, obj_path, iface_name, method_name, params, invocation):
        if method_name == "OpenPipeAndSettings":
            app_id = params.unpack()[0]
            rfd, wfd = os.pipe()
            os.write(wfd, f"portal-ok:{app_id}".encode())
            os.close(wfd)
            fd_list = Gio.UnixFDList.new()
            idx = fd_list.append(rfd)
            os.close(rfd)

            # > 300B body so GDBus 16B header read leaves > 128B in rx_ring!
            settings = {
                "color-scheme": GLib.Variant("u", 1),
                "accent-color": GLib.Variant("s", "blue-" + ("x" * 180)),
                "contrast": GLib.Variant("u", 0),
                "high-contrast": GLib.Variant("b", False),
                "app-echo": GLib.Variant("s", app_id + "-" + ("y" * 120)),
            }
            # Also emit 2 signals (> 200B) back-to-back so dbus-broker batches FD + plain messages!
            for k in range(2):
                connection.emit_signal(
                    sender,
                    "/org/freedesktop/portal/desktop",
                    "org.netzero.PortalTorture",
                    "SettingChanged",
                    GLib.Variant("(sss)", ("org.freedesktop.appearance", f"k{k}", "z" * 180)),
                )
            invocation.return_value_with_unix_fd_list(
                GLib.Variant("(ha{sv})", (idx, settings)), fd_list
            )
        elif method_name == "ReadLargeRegistry":
            seq = params.unpack()[0]
            invocation.return_value(GLib.Variant("(s)", (f"seq={seq}:" + ("R" * 320),)))

    conn.register_object(
        "/org/freedesktop/portal/desktop", iface_info, on_method_call, None, None
    )
    loop = GLib.MainLoop()

    def on_name_acquired(c, name):
        ready_q.put("READY")

    def on_name_lost(c, name):
        ready_q.put("LOST")
        loop.quit()

    Gio.bus_own_name_on_connection(
        conn, "org.netzero.PortalTorture", Gio.BusNameOwnerFlags.NONE,
        on_name_acquired, on_name_lost
    )
    loop.run()


def run_portal_client(bus_addr: str, cid: int, iters: int, res_q: Queue):
    try:
        conn = Gio.DBusConnection.new_for_address_sync(
            bus_addr,
            Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT
            | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,
            None,
            None,
        )
        for i in range(iters):
            ret, out_fds = conn.call_with_unix_fd_list_sync(
                "org.netzero.PortalTorture",
                "/org/freedesktop/portal/desktop",
                "org.netzero.PortalTorture",
                "OpenPipeAndSettings",
                GLib.Variant("(s)", (f"org.gnome.Console.c{cid}.i{i}",)),
                GLib.VariantType("(ha{sv})"),
                Gio.DBusCallFlags.NONE,
                3000,
                None,
                None,
            )
            fd_idx, settings = ret.unpack()
            fd = out_fds.get(fd_idx)
            data = os.read(fd, 64)
            os.close(fd)
            if not data.startswith(b"portal-ok:") or "accent-color" not in settings:
                res_q.put(f"FAIL client {cid} iter {i}: bad data {data!r}")
                return

            reg = conn.call_sync(
                "org.netzero.PortalTorture",
                "/org/freedesktop/portal/desktop",
                "org.netzero.PortalTorture",
                "ReadLargeRegistry",
                GLib.Variant("(u)", (i,)),
                GLib.VariantType("(s)"),
                Gio.DBusCallFlags.NONE,
                3000,
                None,
            )
            if not reg.unpack()[0].startswith(f"seq={i}:"):
                res_q.put(f"FAIL client {cid} iter {i}: bad reg")
                return
        res_q.put("OK")
    except Exception as e:
        res_q.put(f"EXC client {cid}: {e}")


def main():
    iters = int(sys.argv[1]) if len(sys.argv) > 1 else 80
    n_clients = 6
    with tempfile.TemporaryDirectory(prefix="nz_dbus_") as tmpd:
        sock_path = os.path.join(tmpd, "bus.sock")

        lsock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM | socket.SOCK_NONBLOCK)
        lsock.bind(sock_path)
        lsock.listen(64)

        ctrl_parent, ctrl_child = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
        log_parent, log_child = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
        os.set_inheritable(ctrl_child.fileno(), True)
        os.set_inheritable(log_child.fileno(), True)

        broker = subprocess.Popen(
            [
                "/usr/bin/dbus-broker",
                "--log", str(log_child.fileno()),
                "--controller", str(ctrl_child.fileno()),
                "--machine-id", "0123456789abcdef0123456789abcdef",
            ],
            pass_fds=(ctrl_child.fileno(), log_child.fileno()),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        ctrl_child.close()
        log_child.close()

        # SASL EXTERNAL + NEGOTIATE_UNIX_FD + AddListener(lsock, permissive policy)
        ctrl_parent.sendall(b"\x00AUTH EXTERNAL\r\nDATA\r\nNEGOTIATE_UNIX_FD\r\nBEGIN\r\n")
        # Read SASL replies ("DATA\r\nOK ...\r\nAGREE_UNIX_FD\r\n")
        sasl_reply = b""
        while b"AGREE_UNIX_FD\r\n" not in sasl_reply:
            chunk = ctrl_parent.recv(256)
            if not chunk:
                raise RuntimeError("dbus-broker closed controller during SASL")
            sasl_reply += chunk

        add_listener_pkt = (
            b"\x6c\x01\x00\x01\x88\x01\x00\x00\x01\x00\x00\x00\x70\x00\x00\x00"
            b"\x01\x01\x6f\x00\x15\x00\x00\x00/org/bus1/DBus/Broker\x00\x00\x00"
            b"\x03\x01\x73\x00\x0b\x00\x00\x00AddListener\x00\x00\x00\x00\x00"
            b"\x02\x01\x73\x00\x14\x00\x00\x00org.bus1.DBus.Broker\x00\x00\x00\x00"
            b"\x08\x01\x67\x00\x03ohv\x00\x00\x00\x00\x00\x00\x00\x00"
            b"\x09\x01\x75\x00\x01\x00\x00\x00"
            b"\x19\x00\x00\x00/org/bus1/DBus/Listener/0\x00\x00\x00"
            b"\x00\x00\x00\x00"
            b"](a(u(bta(btbs)a(btssssuutt)a(btssssuutt)))a(buu(bta(btbs)a(btssssuutt)a(btssssuutt)))a(ss)bs)\x00\x00"
            b"\x00\x00\x00\x00"
            b"\xd8\x00\x00\x00\x00\x00\x00\x00"
            b"\xff\xff\xff\xff\x00\x00\x00\x00"
            b"\x01\x00\x00\x00\x00\x00\x00\x00\x98\x24\x49\x92\x24\x49\x92\x24"
            b"\x19\x00\x00\x00\x00\x00\x00\x00"
            b"\x01\x00\x00\x00\x00\x00\x00\x00\x97\x24\x49\x92\x24\x49\x92\x24"
            b"\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
            b"\x48\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x95\x24\x49\x92\x24\x49\x92\x24"
            + (b"\x00" * 48)
            + b"\xff\xff\xff\xff\xff\xff\xff\xff"
            + b"\x48\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x96\x24\x49\x92\x24\x49\x92\x24"
            + (b"\x00" * 48)
            + b"\xff\xff\xff\xff\xff\xff\xff\xff"
            + (b"\x00" * 20)
            + b"\x07\x00\x00\x00session\x00"
        )
        ctrl_parent.sendmsg(
            [add_listener_pkt],
            [(socket.SOL_SOCKET, socket.SCM_RIGHTS, struct.pack("i", lsock.fileno()))],
        )
        # Wait for AddListener method_return (16B D-Bus reply header, msg_type == 2)
        reply = ctrl_parent.recv(64)
        if len(reply) < 16 or reply[1] != 2:
            raise RuntimeError(f"AddListener failed: {reply!r}")
        lsock.close()

        bus_addr = f"unix:path={sock_path}"
        ready_q = Queue()
        svc = Process(target=run_portal_service, args=(bus_addr, ready_q))
        svc.start()

        try:
            status = ready_q.get(timeout=5.0)
            if status != "READY":
                raise RuntimeError(f"Service failed to acquire name: {status}")

            res_q = Queue()
            t0 = time.monotonic()
            clients = [
                Process(target=run_portal_client, args=(bus_addr, c, iters, res_q))
                for c in range(n_clients)
            ]
            for p in clients:
                p.start()

            results = [res_q.get(timeout=15.0) for _ in range(n_clients)]
            for p in clients:
                p.join(timeout=2.0)
            dt = time.monotonic() - t0

            bad = [r for r in results if r != "OK"]
            if bad:
                print(f"[FAIL] dbus-broker + GDBus portal torture failed: {bad}", file=sys.stderr)
                sys.exit(1)

            total_calls = n_clients * iters * 2
            print(
                f"[PASS] Real /usr/bin/dbus-broker + GDBus portal torture: "
                f"{n_clients} clients x {iters} loops ({total_calls} RPCs + {n_clients*iters} SCM_RIGHTS FDs + "
                f"{n_clients*iters*2} signals) completed in {dt*1000:.1f} ms (0 timeouts!)"
            )
        finally:
            svc.terminate()
            svc.join(timeout=2.0)
            ctrl_parent.close()
            log_parent.close()
            broker.send_signal(signal.SIGTERM)
            try:
                broker.wait(timeout=3.0)
            except subprocess.TimeoutExpired:
                broker.kill()


if __name__ == "__main__":
    main()
