"""Remove identifying text from a TFLite model without touching any weight or layout.

In place, keeping file size and every offset:
  * every tensor name becomes a neutral "t<index>";
  * the CONVERSION_METADATA blob (converter environment and source-model hash)
    becomes a valid empty table, which TFLite treats as "no conversion metadata".
Operator codes, buffers, shapes, signatures and the remaining metadata are not
modified; the script asserts that every changed byte lies inside those two kinds
of region.

Usage: python -I tools/neutralize-model.py <input.tflite> <output.tflite>
"""
import struct
import sys


def u8(b, p): return b[p]
def u16(b, p): return struct.unpack_from("<H", b, p)[0]
def u32(b, p): return struct.unpack_from("<I", b, p)[0]
def i32(b, p): return struct.unpack_from("<i", b, p)[0]


def field(b, table, index):
    """Absolute position of a table field, or None if absent."""
    vtable = table - i32(b, table)
    if 4 + 2 * index >= u16(b, vtable):
        return None
    offset = u16(b, vtable + 4 + 2 * index)
    return table + offset if offset else None


def ref(b, table, index):
    """Follow an offset field (table, vector or string)."""
    p = field(b, table, index)
    return None if p is None else p + u32(b, p)


def vector(b, v):
    """Element positions of a vector of offsets."""
    return [v + 4 + 4 * i + u32(b, v + 4 + 4 * i) for i in range(u32(b, v))]


def string(b, s):
    return bytes(b[s + 4:s + 4 + u32(b, s)]).decode("utf-8", "replace")


def main(source, target):
    b = bytearray(open(source, "rb").read())
    assert b[4:8] == b"TFL3", "not a TFLite flatbuffer"
    model = u32(b, 0)
    changed = []  # (start, end) of every region we may write

    # Strings that must keep their text even if shared with a tensor name.
    protected = set()
    for sig in vector(b, ref(b, model, 7)) if ref(b, model, 7) else []:
        for maps in (0, 1):
            v = ref(b, sig, maps)
            for entry in vector(b, v) if v else []:
                if ref(b, entry, 0): protected.add(ref(b, entry, 0))
        if ref(b, sig, 2): protected.add(ref(b, sig, 2))
    for meta in vector(b, ref(b, model, 6)) if ref(b, model, 6) else []:
        protected.add(ref(b, meta, 0))
    for code in vector(b, ref(b, model, 1)):
        if ref(b, code, 1): protected.add(ref(b, code, 1))
    if ref(b, model, 3): protected.add(ref(b, model, 3))

    renamed, skipped, seen = 0, 0, set()
    for sub in vector(b, ref(b, model, 2)):
        if ref(b, sub, 4): protected.add(ref(b, sub, 4))
        for index, tensor in enumerate(vector(b, ref(b, sub, 0))):
            s = ref(b, tensor, 3)
            if s is None or s in seen:
                continue
            seen.add(s)
            if s in protected:
                skipped += 1
                continue
            old = u32(b, s)
            new = f"t{index}".encode()
            if len(new) > old:
                new = new[:old]
            end = s + 4 + old + 1
            b[s:end] = struct.pack("<I", len(new)) + new + bytes(end - s - 4 - len(new))
            changed.append((s, end))
            renamed += 1

    cleared = False
    for meta in vector(b, ref(b, model, 6)) if ref(b, model, 6) else []:
        if string(b, ref(b, meta, 0)) != "CONVERSION_METADATA":
            continue
        buffer = vector(b, ref(b, model, 4))[u32(b, field(b, meta, 1))]
        data = ref(b, buffer, 0)
        assert data is not None and field(b, buffer, 1) is None, "external buffer not supported"
        size = u32(b, data)
        assert size >= 12, "conversion metadata too small"
        start = data + 4
        # Root offset -> table at 8; vtable at 4 with no fields; table soffset 4.
        b[start:start + size] = (struct.pack("<IHHi", 8, 4, 4, 4) + bytes(size - 12))
        changed.append((start, start + size))
        cleared = True

    original = open(source, "rb").read()
    assert len(b) == len(original)
    allowed = bytearray(len(b))
    for lo, hi in changed:
        allowed[lo:hi] = b"\x01" * (hi - lo)
    for p in range(len(b)):
        if b[p] != original[p]:
            assert allowed[p], f"unexpected change at {p}"
    open(target, "wb").write(b)
    print(f"renamed_tensors={renamed} kept_shared_names={skipped} conversion_metadata_cleared={cleared}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
