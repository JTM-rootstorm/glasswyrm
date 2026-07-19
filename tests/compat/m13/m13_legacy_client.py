#!/usr/bin/env python3
"""Bounded raw-X11 legacy window controlled by a private local socket."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import socket
import struct


class Client:
    def __init__(self, display: int) -> None:
        path = os.environ.get("GLASSWYRM_X11_SOCKET",
                              f"/tmp/.X11-unix/X{display}")
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.connect(path)
        self.socket.sendall(b"l\0" + struct.pack("<HHHHH", 11, 0, 0, 0, 0))
        prefix = self.read(8)
        body = self.read(struct.unpack_from("<H", prefix, 6)[0] * 4)
        if prefix[0] != 1:
            raise RuntimeError("X11 setup rejected")
        setup = prefix + body
        base = struct.unpack_from("<I", setup, 12)[0]
        mask = struct.unpack_from("<I", setup, 16)[0]
        vendor = struct.unpack_from("<H", setup, 24)[0]
        formats = setup[29]
        screen = 40 + ((vendor + 3) & ~3) + formats * 8
        self.root = struct.unpack_from("<I", setup, screen)[0]
        self.window = base | (1 & mask)
        self.graphics_contexts = [base | (index & mask) for index in range(2, 6)]
        self.net_wm_state = self.intern_atom("_NET_WM_STATE")
        self.net_wm_state_fullscreen = self.intern_atom(
            "_NET_WM_STATE_FULLSCREEN")

    def read(self, count: int) -> bytes:
        value = bytearray()
        while len(value) < count:
            part = self.socket.recv(count - len(value))
            if not part:
                raise RuntimeError("X11 connection closed")
            value.extend(part)
        return bytes(value)

    def request(self, opcode: int, data: int, body: bytes) -> None:
        size = 4 + len(body)
        if size % 4:
            raise RuntimeError("request is not aligned")
        self.socket.sendall(bytes((opcode, data)) + struct.pack("<H", size // 4) + body)

    def intern_atom(self, name: str) -> int:
        encoded = name.encode("ascii")
        body = struct.pack("<H2x", len(encoded))
        body += encoded + bytes((-len(encoded)) % 4)
        self.request(16, 0, body)
        reply = self.read(32)
        if reply[0] != 1:
            raise RuntimeError(f"InternAtom failed for {name}")
        return struct.unpack_from("<I", reply, 8)[0]

    def create(self) -> None:
        body = struct.pack("<IIhhHHHHII", self.window, self.root, 64, 80,
                           320, 240, 0, 1, 0, 2)
        body += struct.pack("<I", 0x285078)
        self.request(1, 0, body)
        for graphics_context, color in zip(
                self.graphics_contexts,
                (0xD03030, 0x30C060, 0x3060D0, 0xF0D040), strict=True):
            self.request(55, 0, struct.pack(
                "<IIII", graphics_context, self.window, 1 << 2, color))
        self.request(8, 0, struct.pack("<I", self.window))
        self.paint(320, 240)
        self.sync()

    def configure(self, x: int, y: int, width: int, height: int) -> None:
        self.request(12, 0, struct.pack("<IH2xIIII", self.window, 0x0f,
                                       x, y, width, height))
        self.sync()
        self.paint(width, height)
        self.sync()

    def paint(self, width: int, height: int) -> None:
        self.request(61, 0, struct.pack("<IhhHH", self.window, 0, 0, 0, 0))
        rectangles = (
            (0, 0, max(1, width // 3), max(1, height // 3)),
            (max(0, width - max(1, width // 4)), 0,
             max(1, width // 4), max(1, height // 5)),
            (0, max(0, height - max(1, height // 4)),
             max(1, width // 5), max(1, height // 4)),
            (max(0, width // 2 - max(1, width // 32)), height // 4,
             max(1, width // 16), max(1, height // 2)),
        )
        for graphics_context, rectangle in zip(
                self.graphics_contexts, rectangles, strict=True):
            self.request(70, 0, struct.pack(
                "<IIhhHH", self.window, graphics_context, *rectangle))

    def fullscreen(self, enabled: bool) -> None:
        event = struct.pack(
            "<BBHII5I", 33, 32, 0, self.window, self.net_wm_state,
            1 if enabled else 0, self.net_wm_state_fullscreen, 0, 1, 0)
        self.request(25, 0, struct.pack("<II", self.root, 0x00180000) + event)
        self.sync()

    def sync(self) -> None:
        self.request(43, 0, b"")
        while True:
            packet = self.read(32)
            if packet[0] == 1:
                extra = struct.unpack_from("<I", packet, 4)[0] * 4
                if extra:
                    self.read(extra)
                return
            if packet[0] == 0:
                raise RuntimeError(f"X11 error {packet[1]}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--display", type=int, default=99)
    parser.add_argument("--control-socket", required=True, type=pathlib.Path)
    parser.add_argument("--ready", required=True, type=pathlib.Path)
    arguments = parser.parse_args()
    arguments.control_socket.unlink(missing_ok=True)
    client = Client(arguments.display)
    listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        client.create()
        listener.bind(str(arguments.control_socket))
        os.chmod(arguments.control_socket, 0o600)
        listener.listen(1)
        arguments.ready.write_text(json.dumps({"window": client.window}) + "\n")
        while True:
            peer, _ = listener.accept()
            with peer:
                command = peer.recv(256).decode("ascii").strip().split()
                if command == ["stop"]:
                    peer.sendall(b"ok\n")
                    break
                if len(command) == 5 and command[0] == "configure":
                    values = [int(value) for value in command[1:]]
                    if (min(values[:2]) < 0 or min(values[2:]) <= 0
                            or max(values[2:]) > 4096):
                        raise ValueError("configuration is outside fixed bounds")
                    client.configure(*values)
                    peer.sendall(b"ok\n")
                elif command in (["fullscreen", "on"],
                                  ["fullscreen", "off"]):
                    client.fullscreen(command[1] == "on")
                    peer.sendall(b"ok\n")
                else:
                    peer.sendall(b"error\n")
    finally:
        listener.close()
        arguments.control_socket.unlink(missing_ok=True)
        client.socket.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
