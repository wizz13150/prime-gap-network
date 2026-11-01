"""Lazy physical work ownership within an immutable logical range.

Each native piece stays within one width, high word and threshold interval.
The first partial class30 at a word boundary has a separate exact certificate;
it never launches a sieve origin in the preceding word.
"""
from dataclasses import dataclass

from pgn_threshold_policy import at_position, verify_pin

BLOCK_POSITIONS = 30 << 25


@dataclass(frozen=True)
class WorkPiece:
    start: int
    end: int
    bits: int
    high_word: int
    minimum_gap: int
    kind: str
    tail_clipped: bool
    policy_sha256: str


def next_piece(pin, position, maximum_positions):
    verify_pin(pin)
    position, maximum_positions = int(position), int(maximum_positions)
    if maximum_positions <= 0:
        raise ValueError("Positive physical work limit required")
    if not int(pin["start"]) <= position < int(pin["end"]):
        raise ValueError("Work cursor outside the pinned range")
    segment = at_position(pin, position)
    high, low = divmod(position, 1 << 64)
    word_end = (high + 1) << 64
    end = min(int(segment["end"]), position + maximum_positions, word_end)
    minimum = int(segment["effective_gap_min"])
    remainder = position % 30
    if low < remainder:
        end = min(end, position + (-remainder) % 30)
        if not 0 < end-position < 30 or minimum < 30:
            raise ValueError("Short-prefix certificate precondition failed")
        kind, clipped = "exact_short_prefix", False
    else:
        base = position - remainder
        blocks = (end-base+BLOCK_POSITIONS-1)//BLOCK_POSITIONS
        kind = "native"
        clipped = base+(blocks+2)*BLOCK_POSITIONS > word_end
    return WorkPiece(position, end, position.bit_length(), high, minimum,
                     kind, clipped, pin["sha256"])


def pieces(pin, maximum_positions):
    position = int(pin["start"])
    while position < int(pin["end"]):
        piece = next_piece(pin, position, maximum_positions)
        yield piece
        position = piece.end
