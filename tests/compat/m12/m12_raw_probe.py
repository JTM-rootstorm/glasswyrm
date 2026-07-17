#!/usr/bin/env python3
"""Raw X11 M12 extension and BIG-REQUESTS probe in both client byte orders."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import socket
import struct


EXTENSIONS = {
    "BIG-REQUESTS": (128, 0, 0),
    "MIT-SHM": (129, 64, 128),
    "XFIXES": (130, 65, 129),
    "DAMAGE": (131, 66, 130),
    "RENDER": (132, 0, 131),
    "COMPOSITE": (133, 0, 0),
    "RANDR": (134, 67, 136),
}


class ProtocolError(RuntimeError):
    pass


class Connection:
    def __init__(self, order: str, display: int) -> None:
        self.order = order
        self.endian = "<" if order == "little" else ">"
        override = os.environ.get("GLASSWYRM_X11_SOCKET")
        path = override or f"/tmp/.X11-unix/X{display}"
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(5.0)
        self.socket.connect(path)
        marker = b"l" if order == "little" else b"B"
        self.socket.sendall(marker + b"\0" + self.pack("HHHHH", 11, 0, 0, 0, 0))
        prefix = self.read_exact(8)
        body = self.read_exact(self.u16(prefix, 6) * 4)
        if prefix[0] != 1:
            raise ProtocolError("X11 setup was rejected")
        setup = prefix + body
        self.resource_base = self.u32(setup, 12)
        self.resource_mask = self.u32(setup, 16)
        vendor_length = self.u16(setup, 24)
        format_count = setup[29]
        screen_offset = 40 + ((vendor_length + 3) & ~3) + format_count * 8
        self.root = self.u32(setup, screen_offset)
        self.next_id = 1

    def close(self) -> None:
        self.socket.close()

    def pack(self, shape: str, *values: int) -> bytes:
        return struct.pack(self.endian + shape, *values)

    def u16(self, data: bytes, offset: int) -> int:
        return struct.unpack_from(self.endian + "H", data, offset)[0]

    def u32(self, data: bytes, offset: int) -> int:
        return struct.unpack_from(self.endian + "I", data, offset)[0]

    def read_exact(self, count: int) -> bytes:
        chunks = bytearray()
        while len(chunks) < count:
            chunk = self.socket.recv(count - len(chunks))
            if not chunk:
                raise ProtocolError("X11 connection closed")
            chunks.extend(chunk)
        return bytes(chunks)

    def packet(self) -> bytes:
        packet = self.read_exact(32)
        if packet[0] == 1:
            packet += self.read_exact(self.u32(packet, 4) * 4)
        return packet

    def request(self, major: int, minor: int = 0, body: bytes = b"") -> None:
        size = 4 + len(body)
        if size % 4:
            raise ProtocolError("request body is not padded")
        self.socket.sendall(bytes((major, minor)) + self.pack("H", size // 4) + body)

    def big_request(self, major: int, minor: int, body: bytes) -> None:
        size = 8 + len(body)
        if size % 4:
            raise ProtocolError("BIG-REQUESTS body is not padded")
        self.socket.sendall(
            bytes((major, minor)) + self.pack("H", 0) + self.pack("I", size // 4) + body
        )

    def xid(self) -> int:
        while self.next_id <= self.resource_mask:
            value = self.resource_base | self.next_id
            self.next_id += 1
            if value != self.root:
                return value
        raise ProtocolError("client resource ID space exhausted")

    def query_extension(self, name: str) -> tuple[bool, int, int, int]:
        encoded = name.encode("ascii")
        padded = encoded + bytes((-len(encoded)) % 4)
        self.request(98, 0, self.pack("HH", len(encoded), 0) + padded)
        reply = self.packet()
        if reply[0] != 1:
            raise ProtocolError(f"QueryExtension({name}) returned an error")
        return bool(reply[8]), reply[9], reply[10], reply[11]

    def expect_reply(self, label: str) -> bytes:
        packet = self.packet()
        if packet[0] != 1:
            raise ProtocolError(f"{label} returned packet type {packet[0]}")
        return packet

    def expect_error(self, code: int, major: int, minor: int, label: str) -> bytes:
        packet = self.packet()
        if packet[0] != 0 or packet[1] != code or packet[10] != major or self.u16(packet, 8) != minor:
            raise ProtocolError(f"{label} returned the wrong X11 error")
        return packet


def registry(connection: Connection, disable_shm: bool) -> dict[str, bool]:
    checks: dict[str, bool] = {}
    for name, expected in EXTENSIONS.items():
        present, major, event, error = connection.query_extension(name)
        if disable_shm and name == "MIT-SHM":
            checks[name] = not present and major == event == error == 0
        else:
            checks[name] = present and (major, event, error) == expected
    return checks


def big_requests(connection: Connection) -> dict[str, bool]:
    present, major, _, _ = connection.query_extension("BIG-REQUESTS")
    if not present:
        raise ProtocolError("BIG-REQUESTS is absent")
    connection.request(major, 0)
    enabled = connection.expect_reply("BIG-REQUESTS Enable")
    maximum_units = connection.u32(enabled, 8)
    pixmap, gc = connection.xid(), connection.xid()
    connection.request(53, 24, connection.pack("IIHH", pixmap, connection.root, 1024, 1024))
    connection.request(55, 0, connection.pack("III", gc, pixmap, 0))
    image = bytes(1024 * 1024 * 4)
    body = connection.pack("IIHHhhBBH", pixmap, gc, 1024, 1024, 0, 0, 0, 24, 0) + image
    connection.big_request(72, 2, body)
    connection.request(43)
    reply = connection.packet()
    if reply[0] == 0:
        raise ProtocolError(f"extended PutImage returned X11 error {reply[1]}")
    if reply[0] != 1:
        raise ProtocolError("extended PutImage synchronization reply is missing")
    connection.request(54, 0, connection.pack("I", pixmap))
    return {
        "enable": maximum_units >= 4 * 1024 * 1024,
        "extended_put_image": True,
    }


def rectangle(connection: Connection, x: int, y: int, width: int, height: int) -> bytes:
    return connection.pack("hhHH", x, y, width, height)


def xfixes_damage(connection: Connection) -> dict[str, bool]:
    checks: dict[str, bool] = {}
    xfixes = connection.query_extension("XFIXES")
    damage = connection.query_extension("DAMAGE")
    if not xfixes[0] or not damage[0]:
        raise ProtocolError("XFIXES or DAMAGE is absent")
    connection.request(xfixes[1], 0, connection.pack("II", 2, 0))
    reply = connection.expect_reply("XFIXES QueryVersion")
    checks["xfixes_version"] = (connection.u32(reply, 8), connection.u32(reply, 12)) == (2, 0)
    connection.request(damage[1], 0, connection.pack("II", 1, 1))
    reply = connection.expect_reply("DAMAGE QueryVersion")
    checks["damage_version"] = (connection.u32(reply, 8), connection.u32(reply, 12)) == (1, 1)

    first, second, output, extents = (connection.xid() for _ in range(4))
    connection.request(xfixes[1], 5, connection.pack("I", first) +
                       rectangle(connection, 0, 0, 32, 32) +
                       rectangle(connection, 40, 0, 16, 16))
    connection.request(xfixes[1], 5, connection.pack("I", second) +
                       rectangle(connection, 16, 0, 32, 32))
    for xid in (output, extents):
        connection.request(xfixes[1], 5, connection.pack("I", xid))
    connection.request(xfixes[1], 13, connection.pack("III", first, second, output))
    connection.request(xfixes[1], 14, connection.pack("III", first, second, output))
    connection.request(xfixes[1], 15, connection.pack("III", first, second, output))
    connection.request(xfixes[1], 17, connection.pack("Ihh", output, 5, 7))
    connection.request(xfixes[1], 18, connection.pack("II", output, extents))
    connection.request(xfixes[1], 19, connection.pack("I", extents))
    fetched = connection.expect_reply("XFIXES FetchRegion")
    checks["region_algebra"] = (
        connection.u32(fetched, 4) == 2
        and struct.unpack_from(connection.endian + "hhHH", fetched, 8) == (5, 7, 56, 32)
        and struct.unpack_from(connection.endian + "hhHH", fetched, 32) == (5, 7, 56, 32)
    )

    connection.request(xfixes[1], 2, connection.pack("III", connection.root, 1, 1))
    connection.request(22, 0, connection.pack("III", connection.root, 1, 0))
    selection_event = connection.packet()
    checks["selection_notify"] = (
        (selection_event[0] & 0x7F) == xfixes[2]
        and connection.u32(selection_event, 4) == connection.root
        and connection.u32(selection_event, 8) == connection.root
        and connection.u32(selection_event, 12) == 1
    )

    damage_id = connection.xid()
    connection.request(damage[1], 1, connection.pack("IIB3x", damage_id, connection.root, 3))
    connection.request(damage[1], 4, connection.pack("II", connection.root, first))
    damage_event = connection.packet()
    checks["damage_notify"] = (
        (damage_event[0] & 0x7F) == damage[2]
        and connection.u32(damage_event, 4) == connection.root
        and connection.u32(damage_event, 8) == damage_id
    )
    connection.request(damage[1], 3, connection.pack("III", damage_id, 0, output))
    connection.request(xfixes[1], 19, connection.pack("I", output))
    parts = connection.expect_reply("DAMAGE Subtract parts")
    checks["damage_subtract"] = connection.u32(parts, 4) > 0
    connection.request(damage[1], 2, connection.pack("I", damage_id))
    for xid in (first, second, output, extents):
        connection.request(xfixes[1], 10, connection.pack("I", xid))
    return checks


def errors(connection: Connection) -> dict[str, bool]:
    xfixes = connection.query_extension("XFIXES")
    damage = connection.query_extension("DAMAGE")
    connection.request(xfixes[1], 0, connection.pack("II", 2, 0))
    connection.expect_reply("XFIXES QueryVersion")
    connection.request(damage[1], 0, connection.pack("II", 1, 1))
    connection.expect_reply("DAMAGE QueryVersion")
    missing = connection.xid()
    connection.request(xfixes[1], 10, connection.pack("I", missing))
    region = connection.expect_error(xfixes[3], xfixes[1], 10, "BadRegion")
    connection.request(damage[1], 2, connection.pack("I", missing))
    bad_damage = connection.expect_error(damage[3], damage[1], 2, "BadDamage")
    connection.request(43)
    continuation = connection.expect_reply("error continuation")
    return {
        "bad_region": connection.u32(region, 4) == missing,
        "bad_damage": connection.u32(bad_damage, 4) == missing,
        "connection_continues": continuation[0] == 1,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--byte-order", choices=("little", "big"), required=True)
    parser.add_argument("--scenario", choices=("registry", "big-requests", "xfixes-damage", "errors"), required=True)
    parser.add_argument("--display", type=int, default=99)
    parser.add_argument("--expect-mit-shm-disabled", action="store_true")
    parser.add_argument("--output", type=pathlib.Path, required=True)
    arguments = parser.parse_args()
    checks: dict[str, bool] = {}
    error = ""
    connection: Connection | None = None
    try:
        connection = Connection(arguments.byte_order, arguments.display)
        if arguments.scenario == "registry":
            checks = registry(connection, arguments.expect_mit_shm_disabled)
        elif arguments.scenario == "big-requests":
            checks = big_requests(connection)
        elif arguments.scenario == "xfixes-damage":
            checks = xfixes_damage(connection)
        else:
            checks = errors(connection)
    except (OSError, ProtocolError, struct.error, ValueError) as failure:
        error = str(failure)
    finally:
        if connection is not None:
            connection.close()
    payload = {
        "schema": 1,
        "probe": "m12_raw_probe",
        "byte_order": arguments.byte_order,
        "scenario": arguments.scenario,
        "checks": checks,
        "passed": bool(checks) and all(checks.values()) and not error,
        "error": error,
    }
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    return 0 if payload["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
