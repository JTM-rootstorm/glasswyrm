#!/usr/bin/env python3
"""Focused raw-wire M12 SHM, RENDER/COMPOSITE, and RANDR scenarios."""

from __future__ import annotations

import ctypes
import os
import struct
from typing import Any


class ProbeFailure(RuntimeError):
    pass


class SysVSegment:
    IPC_PRIVATE = 0
    IPC_CREAT = 0o1000
    IPC_RMID = 0

    def __init__(self, size: int) -> None:
        self.size = size
        self.libc = ctypes.CDLL(None, use_errno=True)
        self.libc.shmget.argtypes = (ctypes.c_int, ctypes.c_size_t, ctypes.c_int)
        self.libc.shmget.restype = ctypes.c_int
        self.libc.shmat.argtypes = (ctypes.c_int, ctypes.c_void_p, ctypes.c_int)
        self.libc.shmat.restype = ctypes.c_void_p
        self.libc.shmdt.argtypes = (ctypes.c_void_p,)
        self.libc.shmdt.restype = ctypes.c_int
        self.libc.shmctl.argtypes = (ctypes.c_int, ctypes.c_int, ctypes.c_void_p)
        self.libc.shmctl.restype = ctypes.c_int
        self.shmid = self.libc.shmget(self.IPC_PRIVATE, size, self.IPC_CREAT | 0o600)
        if self.shmid < 0:
            raise OSError(ctypes.get_errno(), "shmget failed")
        self.address = self.libc.shmat(self.shmid, None, 0)
        if self.address == ctypes.c_void_p(-1).value:
            error = ctypes.get_errno()
            self.libc.shmctl(self.shmid, self.IPC_RMID, None)
            raise OSError(error, "shmat failed")

    def write(self, data: bytes, offset: int = 0) -> None:
        if offset < 0 or offset + len(data) > self.size:
            raise ProbeFailure("shared-memory write is out of bounds")
        ctypes.memmove(self.address + offset, data, len(data))

    def clear(self) -> None:
        ctypes.memset(self.address, 0, self.size)

    def read(self, count: int, offset: int = 0) -> bytes:
        if offset < 0 or offset + count > self.size:
            raise ProbeFailure("shared-memory read is out of bounds")
        return ctypes.string_at(self.address + offset, count)

    def close(self) -> None:
        if self.address not in (None, ctypes.c_void_p(-1).value):
            self.libc.shmdt(self.address)
            self.address = None
        if self.shmid >= 0:
            self.libc.shmctl(self.shmid, self.IPC_RMID, None)
            self.shmid = -1


def _create_pixmap(connection: Any, depth: int, width: int, height: int) -> int:
    pixmap = connection.xid()
    connection.request(
        53, depth, connection.pack("IIHH", pixmap, connection.root, width, height)
    )
    return pixmap


def _create_gc(connection: Any, drawable: int) -> int:
    gc = connection.xid()
    connection.request(55, 0, connection.pack("III", gc, drawable, 0))
    return gc


def _attach(connection: Any, segment: SysVSegment) -> int:
    present, major, event, error = connection.query_extension("MIT-SHM")
    if not present:
        raise ProbeFailure("MIT-SHM is absent")
    xid = connection.xid()
    connection.request(major, 1, connection.pack("IIB3x", xid, segment.shmid, 0))
    connection.sync("MIT-SHM Attach")
    return xid


def _get_image(connection: Any, drawable: int, shmseg: int,
               segment: SysVSegment, width: int = 1, height: int = 1) -> bytes:
    segment.clear()
    connection.request(
        129, 4,
        connection.pack("IhhHHIB3xII", drawable, 0, 0, width, height,
                        0xFFFFFFFF, 2, shmseg, 0),
    )
    reply = connection.expect_reply("MIT-SHM GetImage")
    if reply[1] != 24 or connection.u32(reply, 12) != width * height * 4:
        raise ProbeFailure("MIT-SHM GetImage returned the wrong shape")
    return segment.read(width * height * 4)


def shm_scenario(connection: Any) -> tuple[dict[str, bool], dict[str, Any]]:
    segment = SysVSegment(64)
    pixmap = gc = shmseg = 0
    try:
        present, major, event_base, error_base = connection.query_extension("MIT-SHM")
        if not present:
            raise ProbeFailure("MIT-SHM is absent")
        connection.request(major, 0)
        version = connection.expect_reply("MIT-SHM QueryVersion")
        version_ok = (
            connection.u16(version, 8) == 1
            and connection.u16(version, 10) == 1
            and connection.u16(version, 12) == os.getuid()
            and connection.u16(version, 14) == os.getgid()
            and version[1] == 0
            and version[16] == 2
        )
        pixmap = _create_pixmap(connection, 24, 2, 2)
        gc = _create_gc(connection, pixmap)
        shmseg = _attach(connection, segment)
        pixels = bytes.fromhex("332211006655440099887700ccbbaa00")
        segment.write(pixels)
        connection.request(
            major, 3,
            connection.pack("IIHHHHHHhhBBBxII", pixmap, gc, 2, 2, 0, 0,
                            2, 2, 0, 0, 24, 2, 1, shmseg, 0),
        )
        completion = connection.packet()
        completion_ok = (
            (completion[0] & 0x7F) == event_base
            and connection.u32(completion, 4) == pixmap
            and connection.u16(completion, 8) == 3
            and completion[10] == major
            and connection.u32(completion, 12) == shmseg
        )
        round_trip = _get_image(connection, pixmap, shmseg, segment, 2, 2)
        connection.request(major, 2, connection.pack("I", shmseg))
        shmseg = 0
        connection.request(60, 0, connection.pack("I", gc))
        connection.request(54, 0, connection.pack("I", pixmap))
        connection.sync("MIT-SHM cleanup")
        return {
            "query_version": version_ok,
            "attach_put_get_detach": round_trip == pixels,
            "completion_event": completion_ok,
        }, {
            "version": "1.1",
            "event_base": event_base,
            "error_base": error_base,
            "round_trip_xrgb": [f"0x{int.from_bytes(round_trip[i:i+4], 'little'):08x}"
                                for i in range(0, len(round_trip), 4)],
        }
    finally:
        if shmseg:
            try:
                connection.request(129, 2, connection.pack("I", shmseg))
            except OSError:
                pass
        segment.close()


def render_composite_scenario(
    connection: Any,
) -> tuple[dict[str, bool], dict[str, Any]]:
    render = connection.query_extension("RENDER")
    composite = connection.query_extension("Composite")
    if not render[0] or not composite[0]:
        raise ProbeFailure("RENDER or COMPOSITE is absent")
    segment = SysVSegment(64)
    shmseg = _attach(connection, segment)
    resources: list[tuple[int, int]] = []
    try:
        connection.request(render[1], 0, connection.pack("II", 0, 11))
        version = connection.expect_reply("RENDER QueryVersion")
        connection.request(render[1], 1)
        formats = connection.expect_reply("RENDER QueryPictFormats")
        expected_formats = [0x1FFFF101, 0x1FFFF102, 0x1FFFF103, 0x1FFFF104]
        observed_formats = [connection.u32(formats, offset)
                            for offset in (32, 60, 88, 116)]
        expected_details = [
            (expected_formats[0], 1, 1, (0, 0, 0, 0, 0, 0, 0, 1), 0),
            (expected_formats[1], 1, 8, (0, 0, 0, 0, 0, 0, 0, 0xFF), 0),
            (expected_formats[2], 1, 24,
             (16, 0xFF, 8, 0xFF, 0, 0xFF, 0, 0), 0),
            (expected_formats[3], 1, 32,
             (16, 0xFF, 8, 0xFF, 0, 0xFF, 24, 0xFF), 0),
        ]
        observed_details = []
        for offset in (32, 60, 88, 116):
            observed_details.append((
                connection.u32(formats, offset), formats[offset + 4],
                formats[offset + 5],
                struct.unpack_from(connection.endian + "8H", formats, offset + 8),
                connection.u32(formats, offset + 24),
            ))
        tree_ok = (
            connection.u32(formats, 144) == 4
            and connection.u32(formats, 148) == expected_formats[3]
            and formats[152] == 1 and connection.u16(formats, 154) == 0
            and formats[160] == 8 and connection.u16(formats, 162) == 0
            and formats[168] == 24 and connection.u16(formats, 170) == 1
            and connection.u32(formats, 176) == connection.root_visual
            and connection.u32(formats, 180) == expected_formats[2]
            and formats[184] == 32 and connection.u16(formats, 186) == 0
            and connection.u32(formats, 192) == 0
        )
        formats_ok = (
            len(formats) == 196
            and connection.u32(formats, 8) == 4
            and connection.u32(formats, 12) == 1
            and connection.u32(formats, 16) == 4
            and connection.u32(formats, 20) == 1
            and connection.u32(formats, 24) == 1
            and observed_formats == expected_formats
            and observed_details == expected_details
            and tree_ok
        )

        pixmap = _create_pixmap(connection, 32, 2, 2)
        destination = connection.xid()
        solid = connection.xid()
        resources.extend(((7, destination), (7, solid), (54, pixmap)))
        connection.request(
            render[1], 4,
            connection.pack("IIII", destination, pixmap, expected_formats[3], 0),
        )
        initial = connection.pack("B3xIHHHH", 1, destination, 0x0A0A,
                                  0x1414, 0x1E1E, 0x4040)
        connection.request(render[1], 26, initial + connection.pack("hhHH", 0, 0, 1, 1))
        connection.request(
            render[1], 33,
            connection.pack("IHHHH", solid, 0x4040, 0x2020, 0x1010, 0x8080),
        )
        connection.request(
            render[1], 8,
            connection.pack("B3xIIIhhhhhhHH", 3, solid, 0, destination,
                            0, 0, 0, 0, 0, 0, 1, 1),
        )
        connection.sync("RENDER Composite")
        render_pixel_bytes = _get_image(connection, pixmap, shmseg, segment)
        render_pixel = int.from_bytes(render_pixel_bytes, "little")

        window = connection.xid()
        window_gc = connection.xid()
        named = connection.xid()
        connection.request(
            1, 24,
            connection.pack("IIhhHHHHII", window, connection.root, 0, 0,
                            2, 2, 0, 1, 0, 0),
        )
        connection.request(55, 0, connection.pack("III", window_gc, window, 0))
        image = bytes.fromhex("33221100") * 4
        connection.request(
            72, 2,
            connection.pack("IIHHhhBBH", window, window_gc, 2, 2, 0, 0, 0, 24, 0)
            + image,
        )
        connection.request(composite[1], 2, connection.pack("IB3x", connection.root, 1))
        connection.request(composite[1], 6, connection.pack("II", window, named))
        connection.request(composite[1], 4, connection.pack("IB3x", connection.root, 1))
        connection.sync("COMPOSITE unredirect")
        named_pixels = _get_image(connection, named, shmseg, segment, 2, 2)
        resources.extend(((54, named), (60, window_gc), (4, window)))

        for opcode, xid in resources:
            connection.request(opcode, 0, connection.pack("I", xid))
        connection.request(129, 2, connection.pack("I", shmseg))
        shmseg = 0
        connection.sync("RENDER/COMPOSITE cleanup")
        return {
            "render_version": (connection.u32(version, 8), connection.u32(version, 12))
            == (0, 11),
            "render_formats": formats_ok,
            # MIT-SHM GetImage intentionally exposes XRGB bytes, so alpha is
            # proven by the exact format contract and RGB by this readback.
            "render_exact_over": render_pixel == 0x00452A1F,
            "composite_version": _composite_version(connection, composite[1]),
            "composite_named_lifetime": named_pixels == image,
        }, {
            "render_formats": [f"0x{value:08x}" for value in observed_formats],
            "render_over_pixel": f"0x{render_pixel:08x}",
            "composite_named_pixels_sha_sample": named_pixels[:4].hex(),
        }
    finally:
        if shmseg:
            try:
                connection.request(129, 2, connection.pack("I", shmseg))
            except OSError:
                pass
        segment.close()


def _composite_version(connection: Any, major: int) -> bool:
    connection.request(major, 0, connection.pack("II", 0, 4))
    reply = connection.expect_reply("COMPOSITE QueryVersion")
    return (connection.u32(reply, 8), connection.u32(reply, 12)) == (0, 4)


def randr_scenario(connection: Any) -> tuple[dict[str, bool], dict[str, Any]]:
    present, major, event_base, error_base = connection.query_extension("RANDR")
    if not present:
        raise ProbeFailure("RANDR is absent")
    connection.request(major, 0, connection.pack("II", 1, 3))
    version = connection.expect_reply("RANDR QueryVersion")
    connection.request(major, 8, connection.pack("I", connection.root))
    resources = connection.expect_reply("RANDR GetScreenResources")
    crtc = connection.u32(resources, 32)
    output = connection.u32(resources, 36)
    mode = connection.u32(resources, 40)
    resource_ok = (
        connection.u16(resources, 16) == 1
        and connection.u16(resources, 18) == 1
        and connection.u16(resources, 20) == 1
        and (crtc, output, mode) == (0x101, 0x100, 0x102)
    )
    connection.request(major, 9, connection.pack("II", output, 1))
    output_info = connection.expect_reply("RANDR GetOutputInfo")
    output_ok = (
        connection.u32(output_info, 12) == crtc
        and output_info[24] == 0
        and output_info[44:55] == b"Glasswyrm-1"
    )
    connection.request(major, 20, connection.pack("II", crtc, 1))
    crtc_info = connection.expect_reply("RANDR GetCrtcInfo")
    crtc_ok = connection.u32(crtc_info, 20) == mode and connection.u32(crtc_info, 32) == output
    connection.request(major, 31, connection.pack("I", connection.root))
    primary = connection.expect_reply("RANDR GetOutputPrimary")
    primary_ok = connection.u32(primary, 8) == output
    request = connection.pack("IIIhhIHHI", crtc, 1, 1, 0, 0, mode, 1, 0, output)
    connection.request(major, 21, request)
    same = connection.expect_reply("RANDR SetCrtcConfig idempotent")
    request = connection.pack("IIIhhIHHI", crtc, 1, 1, 1, 0, mode, 1, 0, output)
    connection.request(major, 21, request)
    rejected = connection.expect_reply("RANDR SetCrtcConfig rejected")
    missing = connection.xid()
    connection.request(major, 9, connection.pack("II", missing, 1))
    bad_output = connection.expect_error(error_base, major, 9, "RANDR BadOutput")
    connection.sync("RANDR error continuation")
    return {
        "version": (connection.u32(version, 8), connection.u32(version, 12)) == (1, 3),
        "resources": resource_ok,
        "output_crtc_primary": output_ok and crtc_ok and primary_ok,
        "idempotent_and_rejected_config": same[1] == 0 and rejected[1] == 3,
        "bad_output_recoverable": connection.u32(bad_output, 4) == missing,
    }, {
        "version": "1.3",
        "event_base": event_base,
        "error_base": error_base,
        "output": f"0x{output:08x}",
        "crtc": f"0x{crtc:08x}",
        "mode": f"0x{mode:08x}",
        "output_name": output_info[44:55].decode("ascii", errors="replace"),
    }
