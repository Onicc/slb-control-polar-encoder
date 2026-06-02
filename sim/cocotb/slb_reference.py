"""Python reference model for the SLB control Polar encoder RTL."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re


MAX_N = 1024
MAX_E = 62 * 128


ISEQ = [
    [
        5, 39, 18, 47, 6, 45, 21, 50, 4, 40, 30, 55, 1, 46, 22, 54,
        0, 42, 27, 61, 7, 43, 29, 60, 12, 37, 20, 51, 14, 35, 25, 52,
        3, 44, 16, 56, 10, 32, 23, 49, 13, 41, 28, 58, 9, 31, 26, 53,
        15, 38, 17, 48, 11, 36, 24, 57, 2, 33, 19, 59, 8, 34,
    ],
    [
        5, 36, 21, 49, 4, 37, 20, 52, 14, 35, 27, 61, 2, 32, 28, 53,
        7, 31, 17, 58, 3, 38, 26, 60, 11, 43, 16, 51, 8, 45, 25, 56,
        12, 34, 19, 47, 13, 41, 24, 54, 10, 44, 22, 59, 1, 40, 23, 57,
        0, 46, 18, 48, 6, 42, 29, 55, 15, 33, 30, 50, 9, 39,
    ],
    [
        8, 36, 16, 52, 14, 35, 19, 51, 9, 45, 24, 58, 15, 33, 23, 59,
        11, 38, 30, 48, 12, 34, 29, 57, 6, 42, 20, 47, 4, 39, 21, 56,
        13, 43, 25, 50, 1, 44, 18, 55, 10, 41, 27, 53, 0, 32, 22, 54,
        7, 31, 17, 49, 5, 37, 26, 60, 2, 46, 28, 61, 3, 40,
    ],
]


def load_q1024(path: Path | None = None) -> list[int]:
    if path is None:
        path = Path(__file__).resolve().parents[2] / "rtl" / "slb_polar_tables.vh"
    text = path.read_text(encoding="utf-8")
    values: dict[int, int] = {}
    for index, value in re.findall(r"rel_q\[\s*(\d+)\]\s*=\s*10'd(\d+);", text):
        values[int(index)] = int(value)
    sequence = [values[i] for i in range(1024)]
    if len(sequence) != 1024 or set(sequence) != set(range(1024)):
        raise ValueError("Q_1024 table is not a complete permutation")
    return sequence


Q1024 = load_q1024()


@dataclass
class EncodeDebug:
    bits: list[int]
    n_len: int
    n_exp: int
    npc: int
    npc_wm: int
    m: int
    k0: int
    info_positions: list[int]
    pc_positions: list[int]


def ceil_div(value: int, divisor: int) -> int:
    return 0 if value <= 0 else 1 + ((value - 1) // divisor)


def ceil_div_signed(numerator: int, denominator: int) -> int:
    if numerator >= 0:
        return (numerator + denominator - 1) // denominator
    return numerator // denominator


def ceil_log2(value: int) -> int:
    current = 1
    exp = 0
    while current < value:
        current <<= 1
        exp += 1
    return exp


def compute_t(k: int, kp: int, e0: int, n_len: int) -> int:
    if e0 >= n_len or kp * 4 > 3 * e0:
        return kp
    if k * 16 <= 7 * e0:
        if e0 * 8 < 5 * n_len:
            return ceil_div_signed(kp * (176 * e0 - 86 * n_len), 32 * n_len)
        if e0 * 4 < 3 * n_len:
            return ceil_div_signed(kp * (40 * e0 - n_len), 32 * n_len)
        return ceil_div_signed(kp * (3 * e0 + 5 * n_len), 8 * n_len)
    if e0 * 16 < 9 * n_len:
        return ceil_div_signed(kp * (9 * n_len - 2 * e0), 8 * n_len)
    return ceil_div_signed(kp * (31 * n_len + e0), 32 * n_len)


def derive_parameters(k: int, e0: int, e: int) -> tuple[int, int, int, int]:
    npc = 3 if 18 <= k <= 25 else 0
    npc_wm = 1 if npc and (e + 3 > k + 192) else 0
    n_exp = max(5, min(ceil_log2(e0), 10))
    n_len = 1 << n_exp
    return npc, npc_wm, n_exp, n_len


def select_positions(k: int, e0: int, e: int, n_len: int, npc: int) -> tuple[list[int], int]:
    kp = k + npc
    t = compute_t(k, kp, e0, n_len)
    temp_frozen: set[int] = set()
    if e0 < n_len:
        if k * 16 <= 7 * e0:
            if n_len <= 256:
                limit = ceil_div(3 * n_len - 2 * e0, 4)
            else:
                limit = ceil_div(7 * n_len - 6 * e0, 8)
            temp_frozen.update(range(limit))
        else:
            temp_frozen.update(range(e0, n_len))
            if e > 128:
                temp_frozen.add(n_len // 4)
                temp_frozen.add(n_len // 2)

    available = [q for q in Q1024 if q < n_len and q not in temp_frozen]
    q1 = available[-t:] if t else []
    excluded = set(q1) | set(range(n_len // 2))
    available2 = [q for q in available if q not in excluded]
    q2_count = kp - t
    q2 = available2[-q2_count:] if q2_count else []
    return sorted(q1 + q2), t


def select_pc_bits(info_positions: list[int], npc: int, npc_wm: int, n_len: int) -> list[int]:
    if npc == 0:
        return []
    info_set = set(info_positions)
    info_by_rel = [q for q in Q1024 if q < n_len and q in info_set]
    pc = info_by_rel[: npc - npc_wm]
    if npc_wm:
        qtilde_count = len(info_positions) - npc
        qtilde = info_by_rel[-qtilde_count:]
        qtilde.sort(key=lambda q: (q.bit_count(), -Q1024.index(q)))
        pc.extend(qtilde[:npc_wm])
    return sorted(pc)


def polar_transform(bits: list[int]) -> list[int]:
    out = bits[:]
    step = 1
    n_len = len(out)
    while step < n_len:
        for base in range(0, n_len, step * 2):
            for offset in range(step):
                out[base + offset] ^= out[base + offset + step]
        step *= 2
    return out


def channel_interleave(e_bits: list[int]) -> list[int]:
    btmp: list[int | None] = [None] * MAX_E
    for index, bit in enumerate(e_bits):
        row = index // 62
        col = index % 62
        seq = row % 3
        btmp[row * 62 + ISEQ[seq][col]] = bit
    out: list[int] = []
    used_rows = ceil_div(len(e_bits), 62)
    for bank in range(2):
        for row in range(used_rows):
            for offset in range(31):
                idx = ((row * 2) + bank) * 31 + offset
                if btmp[idx] is not None:
                    out.append(btmp[idx])
    return out


def encode_control_block(c_bits: list[int], e0: int, e: int, rvid: int) -> EncodeDebug:
    k = len(c_bits)
    if not (0 < k <= MAX_N and e0 > 0 and 0 < e <= MAX_E and 0 <= rvid <= 3):
        raise ValueError("invalid input dimensions")
    npc, npc_wm, n_exp, n_len = derive_parameters(k, e0, e)
    if k + npc > n_len:
        raise ValueError("K+nPC exceeds N")

    info_positions, _ = select_positions(k, e0, e, n_len, npc)
    pc_positions = select_pc_bits(info_positions, npc, npc_wm, n_len)
    info_set = set(info_positions)
    pc_set = set(pc_positions)

    u = [0] * n_len
    src_idx = 0
    if npc == 0:
        for idx in range(n_len):
            if idx in info_set:
                u[idx] = c_bits[src_idx]
                src_idx += 1
    else:
        y = [0, 0, 0, 0, 0]
        for idx in range(n_len):
            y = [y[1], y[2], y[3], y[4], y[0]]
            if idx in info_set:
                if idx in pc_set:
                    u[idx] = y[0]
                else:
                    u[idx] = c_bits[src_idx]
                    src_idx += 1
                    y[0] ^= u[idx]

    d = polar_transform(u)
    m = min(e0, n_len) if k * 16 > 7 * e0 else n_len
    if rvid == 0:
        k0 = 0
    elif rvid == 1:
        k0 = (m // (4 * 32)) * 32
    elif rvid == 2:
        k0 = (m // (2 * 32)) * 32
    else:
        k0 = ((3 * m) // (4 * 32)) * 32
    e_bits = [d[(k0 + idx) % m] for idx in range(e)]
    f_bits = channel_interleave(e_bits)
    return EncodeDebug(f_bits, n_len, n_exp, npc, npc_wm, m, k0, info_positions, pc_positions)


def bits_to_int(bits: list[int]) -> int:
    value = 0
    for idx, bit in enumerate(bits):
        value |= (bit & 1) << idx
    return value


def int_to_bits(value: int, length: int) -> list[int]:
    return [(value >> idx) & 1 for idx in range(length)]
