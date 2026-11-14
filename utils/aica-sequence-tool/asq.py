#!/usr/bin/env python3
"""Original private ASQ sequence authoring. No vendor assets or parser."""
import argparse
import json
import os
from pathlib import Path
import struct
import tempfile
import zlib

MAX_PROGRAMS = 128
MAX_EVENTS = 65536
MAX_SIZE = 64 + MAX_PROGRAMS * 32 + MAX_EVENTS * 16
MAGIC = b"KOSASQ\0\0"
KINDS = {"midi": 1, "tempo": 2, "loop": 3, "loop_end": 4, "end": 5}
EVENT = struct.Struct("<IIIBBBB")


def integer(value, low, high):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f"expected integer in {low}..{high}")
    return value


def keys(value, required):
    if type(value) is not dict or set(value) != set(required):
        raise ValueError(f"expected keys {', '.join(required)}")


def validate_events(events):
    """Independent grammar, including reciprocal loop extents and END position."""
    if not 1 <= len(events) <= MAX_EVENTS:
        raise ValueError("event count")
    stack, ticks = [], 0
    for i, (delta, value, auxiliary, kind, status, a, b) in enumerate(events):
        integer(delta, 0, 0xffffffff)
        ticks += delta
        if kind != 1 and (status or a or b):
            raise ValueError("non-MIDI data")
        if kind == 1:
            integer(status, 0x80, 0xef)
            integer(a, 0, 127)
            integer(b, 0, 127)
            if value or auxiliary:
                raise ValueError("MIDI reserved data")
        elif kind == 2:
            integer(value, 1, 60000000)
            if auxiliary:
                raise ValueError("tempo reserved data")
        elif kind == 3:
            integer(value, 0, 65535)
            if len(stack) == 4 or not i < auxiliary < len(events):
                raise ValueError("loop range/depth")
            stack.append((i, auxiliary, ticks))
        elif kind == 4:
            if not stack or auxiliary:
                raise ValueError("unmatched loop end")
            start, end, before = stack.pop()
            if value != start or i != end or ticks == before:
                raise ValueError("loop identity or zero duration")
        elif kind == 5:
            if stack or i != len(events) - 1 or value or auxiliary:
                raise ValueError("END placement/data")
        else:
            raise ValueError("unknown event")
    if events[-1][3] != 5:
        raise ValueError("missing explicit END")


def compile_events(items):
    if type(items) is not list or not 1 <= len(items) <= MAX_EVENTS:
        raise ValueError("event list")
    result, stack = [], []
    fields = {"midi": ("status", "a", "b"), "tempo": ("us",),
              "loop": ("count",), "loop_end": (), "end": ()}
    for i, item in enumerate(items):
        if type(item) is not dict or type(item.get("kind")) is not str or item["kind"] not in fields:
            raise ValueError("event kind")
        kind = item["kind"]
        keys(item, ("delta", "kind") + fields[kind])
        row = [integer(item["delta"], 0, 0xffffffff), 0, 0, KINDS[kind], 0, 0, 0]
        if kind == "midi":
            row[4:] = [integer(item["status"], 0x80, 0xef),
                       integer(item["a"], 0, 127), integer(item["b"], 0, 127)]
        elif kind == "tempo":
            row[1] = integer(item["us"], 1, 60000000)
        elif kind == "loop":
            row[1] = integer(item["count"], 0, 65535)
            if len(stack) == 4:
                raise ValueError("loop depth")
            stack.append(i)
        elif kind == "loop_end":
            if not stack:
                raise ValueError("unmatched loop end")
            row[1] = stack.pop()
            result[row[1]][2] = i
        result.append(row)
    validate_events(result)
    return result


def build(manifest):
    keys(manifest, ("programs",))
    programs = manifest["programs"]
    if type(programs) is not list or not 1 <= len(programs) <= MAX_PROGRAMS:
        raise ValueError("program count")
    directory, payload, total = bytearray(), bytearray(), 0
    start = 64 + len(programs) * 32
    for program in programs:
        keys(program, ("tpqn", "tempo_us", "events"))
        tpqn = integer(program["tpqn"], 1, 9600)
        tempo = integer(program["tempo_us"], 1, 60000000)
        events = compile_events(program["events"])
        total += len(events)
        if total > MAX_EVENTS:
            raise ValueError("aggregate event limit")
        directory += struct.pack("<IIII16x", start + len(payload), len(events), tpqn, tempo)
        payload += b"".join(EVENT.pack(*e) for e in events)
    body = directory + payload
    body += bytes(-(64 + len(body)) % 32)
    header = struct.pack("<8sHHIIIIIII24x", MAGIC, 1, 64, 64 + len(body),
                         len(programs), total, 64, start, zlib.crc32(body), 0)
    return header + body


def inspect(data):
    if not 64 <= len(data) <= MAX_SIZE:
        raise ValueError("file size")
    magic, version, header, size, count, total, directory, start, crc, flags = struct.unpack_from("<8sHHIIIIIII", data)
    if magic != MAGIC or version != 1 or header != 64 or size != len(data):
        raise ValueError("header")
    if not 1 <= count <= MAX_PROGRAMS or not count <= total <= MAX_EVENTS:
        raise ValueError("counts")
    end = start + total * 16
    if directory != 64 or start != 64 + count * 32 or size != (end + 31) // 32 * 32:
        raise ValueError("canonical extents")
    if flags or any(data[40:64]) or any(data[end:]) or crc != zlib.crc32(data[64:]):
        raise ValueError("reserved/padding/CRC")
    programs, first = [], start
    for i in range(count):
        offset, n, tpqn, tempo = struct.unpack_from("<IIII", data, 64 + i * 32)
        if any(data[80 + i * 32:96 + i * 32]) or offset != first or not n or n > (end - first) // 16:
            raise ValueError("program extent")
        integer(tpqn, 1, 9600)
        integer(tempo, 1, 60000000)
        events = [EVENT.unpack_from(data, offset + j * 16) for j in range(n)]
        validate_events(events)
        items = []
        for delta, value, auxiliary, kind, status, a, b in events:
            item = {"delta": delta, "kind": next(k for k, v in KINDS.items() if v == kind)}
            if kind == 1:
                item.update(status=status, a=a, b=b)
            elif kind == 2:
                item["us"] = value
            elif kind == 3:
                item["count"] = value
            items.append(item)
        programs.append({"tpqn": tpqn, "tempo_us": tempo, "events": items})
        first += n * 16
    if first != end:
        raise ValueError("unused events")
    return {"programs": programs}


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def publish(path, data):
    """Atomic, no-clobber same-directory publication, including empty files."""
    path = Path(path)
    fd, temporary = tempfile.mkstemp(prefix=".asq-", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        os.link(temporary, path)
    finally:
        os.unlink(temporary)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    make = sub.add_parser("build")
    make.add_argument("manifest", type=Path)
    make.add_argument("output", type=Path)
    check = sub.add_parser("inspect")
    check.add_argument("bank", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "build":
            with args.manifest.open("rb") as source:
                raw = source.read(8 * 1024 * 1024 + 1)
            if len(raw) > 8 * 1024 * 1024:
                raise ValueError("JSON exceeds8MiB")
            publish(args.output, build(json.loads(raw, object_pairs_hook=unique_object)))
        else:
            with args.bank.open("rb") as source:
                raw = source.read(MAX_SIZE + 1)
            print(json.dumps(inspect(raw), indent=2))
    except (ValueError, TypeError, KeyError, OSError) as error:
        parser.exit(1, f"asq: {error}\n")


if __name__ == "__main__":
    main()
