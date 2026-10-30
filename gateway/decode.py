"""Gateway side: decode, verify, and log mesh packets (must match firmware/lib/seismic/mesh.h)."""
import hashlib
import hmac
import struct
from dataclasses import dataclass

FMT = "<BBBBHIBBH3BBB8sH"      # 29 bytes
SIZE = struct.calcsize(FMT)
LABELS = ["quiet", "vehicle", "footsteps", "wind", "unknown"]
RULES = ["below_noise_gate", "low_band_sustained", "cadence_peak", "high_band_diffuse", "no_rule"]
TAG_OFFSET = SIZE - 10


def crc16(data: bytes) -> int:
    c = 0xFFFF
    for b in data:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if c & 0x8000 else (c << 1)
            c &= 0xFFFF
    return c


@dataclass
class Detection:
    src: int
    seq: int
    t_s: int
    label: str
    rule: str
    rms_g: float
    band: tuple
    cadence: float
    replayed: bool
    hops_left: int
    authentic: bool


def decode(raw: bytes, key: bytes) -> Detection:
    if len(raw) != SIZE:
        raise ValueError(f"expected {SIZE} bytes, got {len(raw)}")
    if crc16(raw[:-2]) != struct.unpack_from("<H", raw, SIZE - 2)[0]:
        raise ValueError("crc mismatch")
    (ver, typ, src, hops, seq, t, label, rule, rms, b0, b1, b2, cad, rep, tag, _crc) = struct.unpack(FMT, raw)
    auth = bytearray(raw[:TAG_OFFSET])
    auth[3] = 0                              # hops_left is excluded from the tag
    mac = hmac.new(key, bytes(auth), hashlib.sha256).digest()[:8]
    return Detection(src, seq, t, LABELS[label], RULES[rule], rms / 1e6, (b0, b1, b2), cad / 100, bool(rep), hops,
                     hmac.compare_digest(mac, tag))


class Deduper:
    """Floods deliver copies via several relays; keep the first (src, seq)."""

    def __init__(self, size=4096):
        self.seen, self.size = [], size

    def fresh(self, d: Detection) -> bool:
        k = (d.src, d.seq)
        if k in self.seen:
            return False
        self.seen = (self.seen + [k])[-self.size:]
        return True
