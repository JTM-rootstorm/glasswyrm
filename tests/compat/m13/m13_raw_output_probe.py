#!/usr/bin/env python3
"""Raw little/big-endian RANDR and GW_SCALE multi-output probe."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import socket
import struct
import time


class ProtocolError(RuntimeError):
    pass


class Connection:
    def __init__(self, order: str, display: int) -> None:
        self.order = order
        self.endian = "<" if order == "little" else ">"
        path = os.environ.get("GLASSWYRM_X11_SOCKET",
                              f"/tmp/.X11-unix/X{display}")
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(5)
        self.socket.connect(path)
        marker = b"l" if order == "little" else b"B"
        self.socket.sendall(marker + b"\0" + self.pack("HHHHH", 11, 0, 0, 0, 0))
        prefix = self.read(8)
        body = self.read(self.u16(prefix, 6) * 4)
        if prefix[0] != 1:
            raise ProtocolError("X11 setup rejected")
        setup = prefix + body
        vendor_length = self.u16(setup, 24)
        format_count = setup[29]
        screen = 40 + ((vendor_length + 3) & ~3) + format_count * 8
        self.root = self.u32(setup, screen)

    def pack(self, shape: str, *values: int) -> bytes:
        return struct.pack(self.endian + shape, *values)

    def u16(self, data: bytes, offset: int) -> int:
        return struct.unpack_from(self.endian + "H", data, offset)[0]

    def u32(self, data: bytes, offset: int) -> int:
        return struct.unpack_from(self.endian + "I", data, offset)[0]

    def i32(self, data: bytes, offset: int) -> int:
        return struct.unpack_from(self.endian + "i", data, offset)[0]

    def read(self, count: int) -> bytes:
        result = bytearray()
        while len(result) < count:
            part = self.socket.recv(count - len(result))
            if not part:
                raise ProtocolError("X11 connection closed")
            result.extend(part)
        return bytes(result)

    def packet(self) -> bytes:
        value = self.read(32)
        if value[0] == 1:
            value += self.read(self.u32(value, 4) * 4)
        return value

    def request(self, major: int, minor: int, body: bytes = b"") -> bytes:
        if len(body) % 4:
            raise ProtocolError("request body is not padded")
        self.socket.sendall(bytes((major, minor)) +
                            self.pack("H", (4 + len(body)) // 4) + body)
        reply = self.packet()
        if reply[0] != 1:
            raise ProtocolError(f"opcode {major}.{minor} returned error {reply[1]}")
        return reply

    def send_void(self, major: int, minor: int, body: bytes = b"") -> None:
        if len(body) % 4:
            raise ProtocolError("request body is not padded")
        self.socket.sendall(bytes((major, minor)) +
                            self.pack("H", (4 + len(body)) // 4) + body)

    def query_extension(self, name: str) -> tuple[int, int, int]:
        encoded = name.encode("ascii")
        reply = self.request(98, 0, self.pack("HH", len(encoded), 0) +
                             encoded + bytes((-len(encoded)) % 4))
        if not reply[8]:
            raise ProtocolError(f"required extension {name} is absent")
        return reply[9], reply[10], reply[11]


def randr(connection: Connection, event_ready: pathlib.Path | None,
          event_trigger: pathlib.Path | None) -> dict:
    major, event, error = connection.query_extension("RANDR")
    version = connection.request(major, 0, connection.pack("II", 1, 3))
    resources = connection.request(major, 25, connection.pack("I", connection.root))
    crtc_count = connection.u16(resources, 16)
    output_count = connection.u16(resources, 18)
    mode_count = connection.u16(resources, 20)
    timestamp = connection.u32(resources, 12)
    crtcs = [connection.u32(resources, 32 + index * 4)
             for index in range(crtc_count)]
    cursor = 32 + crtc_count * 4
    outputs = [connection.u32(resources, cursor + index * 4)
               for index in range(output_count)]
    primary = connection.request(major, 31, connection.pack("I", connection.root))
    records = []
    for output in outputs:
        info = connection.request(major, 9, connection.pack("II", output, timestamp))
        name_length = connection.u16(info, 34)
        variable = 36 + connection.u16(info, 26) * 4 + \
            connection.u16(info, 28) * 4 + connection.u16(info, 32) * 4
        name = info[variable:variable + name_length].decode("ascii")
        records.append({"xid": output, "name": name,
                        "crtc": connection.u32(info, 12),
                        "connected": info[24] == 0})
    crtc_records = []
    for crtc in crtcs:
        info = connection.request(major, 20,
                                  connection.pack("II", crtc, timestamp))
        crtc_records.append({
            "xid": crtc, "x": connection.u16(info, 12),
            "y": connection.u16(info, 14),
            "width": connection.u16(info, 16),
            "height": connection.u16(info, 18),
            "mode": connection.u32(info, 20),
            "rotation": connection.u16(info, 24),
        })
    selected_events: list[dict[str, int]] = []
    if event_ready is not None and event_trigger is not None:
        connection.send_void(major, 4,
                             connection.pack("IH2x", connection.root, 0x7))
        connection.request(43, 0)
        event_ready.write_text("ready\n", encoding="ascii")
        for _ in range(500):
            if event_trigger.exists():
                break
            time.sleep(0.01)
        else:
            raise ProtocolError("timed out waiting for RANDR event trigger")
        for _ in range(1 + crtc_count + output_count):
            packet = connection.packet()
            if packet[0] == event:
                selected_events.append({"event": packet[0], "subcode": -1})
            elif packet[0] == event + 1:
                selected_events.append({"event": packet[0],
                                        "subcode": packet[1]})
            else:
                raise ProtocolError(
                    f"unexpected packet {packet[0]} while reading RANDR events")
    event_checks = (not selected_events or
                    ({item["event"] for item in selected_events}
                     == {event, event + 1} and
                     {item["subcode"] for item in selected_events
                      if item["event"] == event + 1} == {0, 1}))
    checks = {
        "version_1_3": (connection.u32(version, 8),
                        connection.u32(version, 12)) == (1, 3),
        "two_outputs": output_count == 2,
        "two_crtcs": crtc_count == 2,
        "two_modes": mode_count >= 2,
        "names": [record["name"] for record in records] == ["LEFT", "RIGHT"],
        "crtc_geometry": [(record["x"], record["y"], record["width"],
                            record["height"]) for record in crtc_records] ==
                         [(0, 0, 640, 480), (640, 0, 640, 480)],
        "primary": connection.u32(primary, 8) == outputs[0],
        "selected_events": event_checks and (event_ready is None or
                                               bool(selected_events)),
        "stable_registry": (major, event, error) == (134, 67, 136),
    }
    return {"checks": checks, "evidence": {
        "version": "1.3", "timestamp": timestamp, "outputs": records,
        "crtcs": crtc_records, "selected_events": selected_events,
        "primary": connection.u32(primary, 8), "event_base": event,
        "error_base": error,
    }}


def gw_scale(connection: Connection) -> dict:
    scale_major, event, error = connection.query_extension("GW_SCALE")
    randr_major, _, _ = connection.query_extension("RANDR")
    version = connection.request(scale_major, 0, connection.pack("II", 0, 1))
    resources = connection.request(randr_major, 25,
                                   connection.pack("I", connection.root))
    crtc_count = connection.u16(resources, 16)
    output_count = connection.u16(resources, 18)
    cursor = 32 + crtc_count * 4
    output_xids = [connection.u32(resources, cursor + index * 4)
                   for index in range(output_count)]
    outputs = []
    for xid in output_xids:
        reply = connection.request(scale_major, 2, connection.pack("I", xid))
        outputs.append({
            "randr_xid": xid,
            "id": f"{connection.u32(reply, 8):08x}{connection.u32(reply, 12):08x}",
            "logical": [connection.i32(reply, 16), connection.i32(reply, 20),
                        connection.u32(reply, 24), connection.u32(reply, 28)],
            "physical": [connection.u32(reply, 32), connection.u32(reply, 36)],
            "scale": [connection.u32(reply, 40), connection.u32(reply, 44)],
            "transform": connection.u16(reply, 48),
            "primary": bool(reply[50]), "enabled": bool(reply[51]),
            "generation": (connection.u32(reply, 52) << 32) |
                          connection.u32(reply, 56),
        })
    checks = {
        "version_0_1": (connection.u32(version, 8),
                        connection.u32(version, 12)) == (0, 1),
        "two_outputs": len(outputs) == 2,
        "stable_ids": len({value["id"] for value in outputs}) == 2,
        "one_primary": sum(value["primary"] for value in outputs) == 1,
        "enabled": all(value["enabled"] for value in outputs),
        "same_generation": len({value["generation"] for value in outputs}) == 1,
        "stable_registry": (scale_major, event, error) == (135, 69, 139),
    }
    return {"checks": checks, "evidence": {"version": "0.1",
        "event_base": event, "error_base": error, "outputs": outputs}}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--byte-order", choices=("little", "big"), required=True)
    parser.add_argument("--scenario", choices=("randr", "gw-scale"), required=True)
    parser.add_argument("--display", type=int, default=99)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--event-ready", type=pathlib.Path)
    parser.add_argument("--event-trigger", type=pathlib.Path)
    arguments = parser.parse_args()
    payload = {"schema": 1, "probe": "m13_raw_output_probe",
               "byte_order": arguments.byte_order,
               "scenario": arguments.scenario, "checks": {},
               "evidence": {}, "passed": False, "errors": []}
    connection = None
    try:
        connection = Connection(arguments.byte_order, arguments.display)
        if ((arguments.event_ready is None) !=
                (arguments.event_trigger is None)):
            raise ProtocolError("RANDR event ready and trigger paths are paired")
        if arguments.scenario != "randr" and arguments.event_ready is not None:
            raise ProtocolError("event proof is RANDR-only")
        result = (randr(connection, arguments.event_ready,
                        arguments.event_trigger)
                  if arguments.scenario == "randr" else gw_scale(connection))
        payload.update(result)
        payload["passed"] = bool(payload["checks"]) and all(payload["checks"].values())
    except (OSError, ProtocolError, UnicodeError, struct.error, ValueError) as error:
        payload["errors"] = [str(error)]
    finally:
        if connection is not None:
            connection.socket.close()
    arguments.output.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    return 0 if payload["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
