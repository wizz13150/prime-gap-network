"""Proof-carrying composite marks; no process, wallet or GPU control.

Safety is checked by exact factor multiplication. Accepted marks do not certify
that every pseudoprime was found. That coverage obligation remains separate.
"""
from array import array
from dataclasses import dataclass
import json
import re
import sys

DESCRIPTOR_SPAN = 30 << 25
DESCRIPTOR_SLOTS = 1 << 28
TILE_SLOTS = 1 << 19
TILES_PER_DESCRIPTOR = DESCRIPTOR_SLOTS // TILE_SLOTS
CLASSES = (1, 7, 11, 13, 17, 19, 23, 29)
CLASS_INDEX = {value: index for index, value in enumerate(CLASSES)}
MAX_PACKET_BYTES = 2 * 1024 * 1024
MAX_EVENTS = 65536


def _integer(value, maximum):
    if not isinstance(value, str) or not re.fullmatch(r"0|[1-9][0-9]{0,38}", value):
        raise ValueError("Noncanonical witness integer")
    number = int(value)
    if number > maximum:
        raise ValueError("Witness integer exceeds its domain")
    return number


def _u32_bytes(values):
    if values.itemsize != 4:
        raise ValueError("Native page requires 32-bit unsigned words")
    if sys.byteorder != "little":
        values.byteswap()
    return values.tobytes()


@dataclass(frozen=True)
class NativeCompositePage:
    job_id: str
    start: int
    length: int
    event_count: int
    descriptor_count: int
    offsets_le: bytes
    slots_le: bytes
    complete_psp_coverage: bool = False

    def assert_context(self, job_id, start, length):
        if (self.job_id, self.start, self.length) != (job_id, start, length):
            raise ValueError("Composite page belongs to another job/cohort")
        if self.complete_psp_coverage:
            raise ValueError("Composite witnesses cannot assert complete PSP coverage")

    def tile_masks(self, descriptor, tile):
        """CPU verifier of the same CSR addresses used by the native OR hook."""
        if not 0 <= descriptor < self.descriptor_count or not 0 <= tile < TILES_PER_DESCRIPTOR:
            raise ValueError("Native tile outside this cohort")
        offsets = array("I"); offsets.frombytes(self.offsets_le)
        slots = array("I"); slots.frombytes(self.slots_le)
        if sys.byteorder != "little":
            offsets.byteswap(); slots.byteswap()
        owner = descriptor * TILES_PER_DESCRIPTOR + tile
        masks = {}
        for slot in slots[offsets[owner]:offsets[owner + 1]]:
            word, bit = divmod(slot, 64)
            masks[word] = masks.get(word, 0) | (1 << bit)
        return masks


def native_composite_page(packet, *, job_id, start, length):
    if not isinstance(job_id, str) or not re.fullmatch(r"[A-Za-z0-9_.:-]{1,128}", job_id):
        raise ValueError("Explicit bounded job identity required")
    # A wheel-aligned descriptor may begin up to29 integers before the first
    # 67-bit search value; those halo composites remain safe to mark.
    if type(start) is not int or type(length) is not int or start < (1 << 66) - 29 or start % 30 or not 0 < length <= 10**12 or start + length > (1 << 95):
        raise ValueError("Invalid exact native cohort")
    if not isinstance(packet, bytes) or len(packet) > MAX_PACKET_BYTES:
        raise ValueError("Composite witness packet exceeds its byte bound")
    def unique(items):
        out = {}
        for key, value in items:
            if key in out:
                raise ValueError("Duplicate witness field")
            out[key] = value
        return out
    try:
        value = json.loads(packet, object_pairs_hook=unique)
    except (UnicodeError, json.JSONDecodeError, RecursionError) as error:
        raise ValueError("Invalid composite witness JSON") from error
    if not isinstance(value, dict) or set(value) != {"schema", "start", "length", "max_a", "max_b", "complete_PSP_coverage", "events"}:
        raise ValueError("Unknown composite witness schema fields")
    if value["schema"] != "affine-composite-witnesses-1" or value["start"] != str(start) or value["length"] != str(length) or value["complete_PSP_coverage"] is not False:
        raise ValueError("Wrong witness cohort or unsupported coverage assertion")
    if type(value["max_a"]) is not int or type(value["max_b"]) is not int or value["max_a"] != 1024 or value["max_b"] != 16:
        raise ValueError("Unsupported bounded affine family profile")
    rows = value["events"]
    if not isinstance(rows, list) or len(rows) > MAX_EVENTS:
        raise ValueError("Composite witness count exceeds its bound")
    descriptors = (length + DESCRIPTOR_SPAN - 1) // DESCRIPTOR_SPAN
    counts = array("I", [0]) * (descriptors * TILES_PER_DESCRIPTOR)
    marks = []
    previous = -1
    for row in rows:
        if not isinstance(row, list) or len(row) != 3:
            raise ValueError("Invalid composite witness record")
        offset = _integer(row[0], length - 1)
        number = start + offset
        # Factors are verified here using exact Python integers; only offsets
        # reach the GPU. A uint64 limit would reject valid small-factor
        # certificates near the top of the 95-bit domain for no safety benefit.
        p, q = _integer(row[1], number - 1), _integer(row[2], number - 1)
        if offset <= previous or p <= 1 or q <= 1 or p * q != number or number % 30 not in CLASS_INDEX:
            raise ValueError("Composite witness is false, unordered or outside the native wheel")
        previous = offset
        descriptor, local = divmod(offset, DESCRIPTOR_SPAN)
        slot = (local // 30) * 8 + CLASS_INDEX[number % 30]
        owner = descriptor * TILES_PER_DESCRIPTOR + slot // TILE_SLOTS
        if descriptor >= descriptors or owner >= len(counts) or slot >= DESCRIPTOR_SLOTS:
            raise ValueError("Composite witness escaped descriptor ownership")
        counts[owner] += 1
        marks.append((owner, slot % TILE_SLOTS))
    offsets = array("I", [0])
    for count in counts:
        offsets.append(offsets[-1] + count)
    slots = array("I", [0]) * len(marks)
    cursor = offsets[:-1]
    for owner, slot in marks:
        slots[cursor[owner]] = slot
        cursor[owner] += 1
    if offsets[-1] != len(rows):
        raise ValueError("Composite page lost an event")
    return NativeCompositePage(job_id, start, length, len(rows), descriptors, _u32_bytes(offsets), _u32_bytes(slots))
