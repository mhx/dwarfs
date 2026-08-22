#!/usr/bin/env python3
"""
Merge a mkdwarfs console log with a memory-usage TSV log into a single,
time-ordered stream with normalized timestamps.

  * timestamps are normalized so that the first line of the console log is at
    time zero; console timestamps are time-of-day and wrap around at midnight
  * memory-log lines are prefixed with 'M' instead of a log level
  * memory figures are column-aligned and converted to MiB/GiB
  * selected console sections get an added aggregate ("total") figure

See --help for all options.
"""

import argparse
import re
import sys

try:  # don't traceback when piped into head/less
    import signal
    signal.signal(signal.SIGPIPE, signal.SIG_DFL)
except (ImportError, AttributeError, ValueError):
    pass

# --------------------------------------------------------------------------
# parsing
# --------------------------------------------------------------------------

# "V 07:48:21.612514 [scanner.cpp:738] entry storage (before freezing):"
RE_HEADER = re.compile(
    r"^(?P<level>[A-Z]) "
    r"(?P<h>\d{2}):(?P<m>\d{2}):(?P<s>\d{2})\.(?P<us>\d{6}) "
    r"(?P<tag>\[[^\]]*\]) "
)

# "V ...............                   shared entry data: 5.276 MiB"
RE_CONT = re.compile(r"^(?P<level>[A-Z]) (?P<dots>\.{3,})")

# "shared entry data: 5.276 MiB" / "hardlinks: 15.97 KiB (265/511 entries, ...)"
# / "shared entry data: 142.8 MiB (capacity: 157.9 MiB)"
RE_SIZE_ITEM = re.compile(
    r"^(?P<indent>\s*)(?P<label>.+?):\s+"
    r"(?P<num>\d[\d,]*(?:\.\d+)?)\s*(?P<unit>[KMGTPE]?i?B)(?![\w.])"
)

RE_CAPACITY = re.compile(
    r"\(capacity:\s*(?P<num>\d[\d,]*(?:\.\d+)?)\s*(?P<unit>[KMGTPE]?i?B)\s*\)"
)

UNIT_FACTOR = {"B": 1}
for _i, _u in enumerate(["KiB", "MiB", "GiB", "TiB", "PiB", "EiB"], start=1):
    UNIT_FACTOR[_u] = 1024 ** _i
for _i, _u in enumerate(["KB", "MB", "GB", "TB", "PB", "EB"], start=1):
    UNIT_FACTOR[_u] = 1000 ** _i

# console sections that get an aggregate figure appended to their header line
DEFAULT_AGGREGATE_SECTIONS = [
    "entry storage (before freezing):",
    "inode storage (before freezing):",
    "file scanner table stats:",
    "entry storage (after dropping file digests):",
    "entry/inode storage:",
]


class Block:
    """One console log statement: a header line plus its continuation lines."""

    __slots__ = ("level", "tod", "tag", "msg", "msg_col", "cont", "trailer")

    def __init__(self, level, tod, tag, msg, msg_col):
        self.level = level
        self.tod = tod          # seconds since midnight (float)
        self.tag = tag
        self.msg = msg
        self.msg_col = msg_col  # column at which the message starts, in the input
        self.cont = []          # list of (relative_indent, text)
        self.trailer = []       # verbatim lines without any log prefix


def parse_console(path):
    """Return (blocks, prologue) where prologue holds any leading unprefixed lines."""
    blocks = []
    prologue = []

    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for raw in fh:
            line = raw.rstrip("\n").rstrip("\r")

            m = RE_HEADER.match(line)
            if m:
                tod = (
                    int(m.group("h")) * 3600
                    + int(m.group("m")) * 60
                    + int(m.group("s"))
                    + int(m.group("us")) / 1e6
                )
                blocks.append(
                    Block(m.group("level"), tod, m.group("tag"),
                          line[m.end():], m.end())
                )
                continue

            m = RE_CONT.match(line)
            if m and blocks:
                blk = blocks[-1]
                body = line[m.end("dots"):]
                # leading padding of the input body, relative to the message column
                pad = len(body) - len(body.lstrip(" "))
                base = blk.msg_col - m.end("dots")
                blk.cont.append((pad - base, body.strip()))
                continue

            # lines with no log prefix at all (e.g. the final mkdwarfs summary)
            if blocks:
                blocks[-1].trailer.append(line)
            else:
                prologue.append(line)

    return blocks, prologue


def parse_memory(path):
    """Return (column_names, rows) with rows as (time, {col: bytes})."""
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        header = fh.readline().rstrip("\n").split("\t")
        if not header or header[0] != "time":
            sys.exit(f"{path}: expected a 'time' column first, got {header!r}")
        cols = header[1:]
        rows = []
        for lineno, raw in enumerate(fh, start=2):
            line = raw.rstrip("\n")
            if not line.strip():
                continue
            fields = line.split("\t")
            if len(fields) != len(header):
                sys.exit(f"{path}:{lineno}: expected {len(header)} fields, "
                         f"got {len(fields)}")
            try:
                t = float(fields[0])
                vals = {c: float(v) for c, v in zip(cols, fields[1:])}
            except ValueError as e:
                sys.exit(f"{path}:{lineno}: {e}")
            rows.append((t, vals))
    return cols, rows


# --------------------------------------------------------------------------
# time handling
# --------------------------------------------------------------------------

def unwrap_midnight(blocks):
    """Convert time-of-day stamps to a monotonic timeline, handling midnight."""
    day = 0.0
    prev = None
    out = []
    for blk in blocks:
        tod = blk.tod
        if prev is not None and tod + day < prev - 1.0:
            # went backwards by more than a second: assume a midnight wrap
            day += 86400.0
        t = tod + day
        prev = t
        out.append(t)
    return out


def fmt_timestamp(t, style):
    """Format a relative time. Negative values are rendered with a '-' sign."""
    sign = "-" if t < 0 else ""
    t = abs(t)
    if style == "hms":
        us = int(round(t * 1e6))
        s, us = divmod(us, 1_000_000)
        m, s = divmod(s, 60)
        h, m = divmod(m, 60)
        body = f"{h:02d}:{m:02d}:{s:02d}.{us:06d}"
        return (sign + body) if sign else body
    # seconds, right aligned with room for 99999 seconds
    return f"{sign + f'{t:.6f}':>12}"


def timestamp_width(style):
    return 15 if style == "hms" else 12


# --------------------------------------------------------------------------
# size formatting
# --------------------------------------------------------------------------

def parse_size(num, unit):
    return float(num.replace(",", "")) * UNIT_FACTOR[unit]


def fmt_size_auto(n):
    """Format like mkdwarfs does: 4 significant digits, binary units."""
    units = ["B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"]
    i = 0
    v = float(n)
    while v >= 1024.0 and i < len(units) - 1:
        v /= 1024.0
        i += 1
    if i == 0:
        return f"{int(round(v))} B"
    if v >= 100:
        s = f"{v:.1f}"
    elif v >= 10:
        s = f"{v:.2f}"
    else:
        s = f"{v:.3f}"
    if "." in s:
        s = s.rstrip("0").rstrip(".")
    return f"{s} {units[i]}"


def fmt_size_fixed(n, unit, decimals):
    return f"{n / UNIT_FACTOR[unit]:.{decimals}f} {unit}"


def size_field_width(unit, decimals):
    """Width for values up to 999.<decimals> <unit>."""
    return 3 + (1 + decimals if decimals > 0 else 0) + 1 + len(unit)


def pct_field_width(decimals):
    """Width of the '[ 80.7%]' field, allowing for 100.0%."""
    return 3 + (1 + decimals if decimals > 0 else 0) + 1 + 2


# --------------------------------------------------------------------------
# extrema
# --------------------------------------------------------------------------

def find_extrema(vals, prominence):
    """Find local maxima/minima with at least `prominence` prominence.

    Uses hysteresis: a candidate peak is only confirmed once the series has
    dropped `prominence` below it (and vice versa for troughs).  This yields
    strictly alternating extrema and ignores sampling noise.  Endpoints are
    never reported.  Returns (maxima, minima) as sets of indices.
    """
    maxima, minima = set(), set()
    if len(vals) < 3 or prominence <= 0:
        return maxima, minima

    idx, val = 0, vals[0]
    rising = None  # None until the series has moved by `prominence`

    for i in range(1, len(vals)):
        v = vals[i]
        if rising is None:
            if v > val + prominence:
                rising, idx, val = True, i, v
            elif v < val - prominence:
                rising, idx, val = False, i, v
            elif v > val:
                idx, val = i, v
            continue
        if rising:
            if v >= val:
                idx, val = i, v
            elif v < val - prominence:
                maxima.add(idx)
                rising, idx, val = False, i, v
        else:
            if v <= val:
                idx, val = i, v
            elif v > val + prominence:
                minima.add(idx)
                rising, idx, val = True, i, v

    maxima.discard(0)
    minima.discard(0)
    maxima.discard(len(vals) - 1)
    minima.discard(len(vals) - 1)
    return maxima, minima


def analyze_columns(rows, cols, prominence_pct):
    """Return {col: (max_value, global_max_index, maxima, minima)}."""
    stats = {}
    for c in cols:
        vals = [r[1][c] for r in rows]
        if not vals:
            stats[c] = (0.0, -1, set(), set())
            continue
        gmax = max(vals)
        gidx = vals.index(gmax)
        maxima, minima = find_extrema(vals, gmax * prominence_pct / 100.0)
        maxima.discard(gidx)
        minima.discard(gidx)
        stats[c] = (gmax, gidx, maxima, minima)
    return stats


# --------------------------------------------------------------------------
# aggregation
# --------------------------------------------------------------------------

def aggregate_block(blk):
    """Sum the size figures at the shallowest indent level of a block.

    Returns (total, capacity_total, count, n_with_capacity) or None if the
    block has no size items.  A missing capacity means the capacity equals the
    size, so such items contribute their size to the capacity total.  The
    capacity total is only meaningful if at least one item reported one, hence
    n_with_capacity.
    """
    items = []
    for indent, text in blk.cont:
        m = RE_SIZE_ITEM.match(text)
        if not m:
            continue
        size = parse_size(m.group("num"), m.group("unit"))
        c = RE_CAPACITY.search(text, m.end())
        cap = parse_size(c.group("num"), c.group("unit")) if c else None
        items.append((indent, size, cap))
    if not items:
        return None
    top = min(i for i, _, _ in items)
    sel = [(s, c) for i, s, c in items if i == top]
    total = sum(s for s, _ in sel)
    cap_total = sum(c if c is not None else s for s, c in sel)
    n_cap = sum(1 for _, c in sel if c is not None)
    return total, cap_total, len(sel), n_cap


def wants_aggregate(blk, patterns, aggregate_all):
    if not blk.cont:
        return False
    if aggregate_all:
        return True
    return any(p in blk.msg for p in patterns)


# --------------------------------------------------------------------------
# emitting
# --------------------------------------------------------------------------

def emit_console_block(out, blk, t, args, ts_width, dots):
    ts = fmt_timestamp(t, args.time_format)
    msg = blk.msg

    if wants_aggregate(blk, args.aggregate, args.aggregate_all):
        agg = aggregate_block(blk)
        if agg:
            total, cap_total, count, n_cap = agg

            def render(n):
                if args.agg_unit == "auto":
                    return fmt_size_auto(n)
                return fmt_size_fixed(n, args.agg_unit, args.decimals)

            parts = [f"total: {render(total)}"]
            if args.capacity and n_cap:
                parts.append(f"capacity: {render(cap_total)}")
                if args.capacity_percent and cap_total:
                    parts[-1] += f", used: {total / cap_total * 100:.1f}%"
            if args.agg_count:
                parts[0] += f" of {count}"
            msg = msg + " [" + ", ".join(parts) + "]"

    out.write(f"{blk.level} {ts} {blk.tag} {msg}\n")

    # message column of the *output*: level + space + ts + space + tag + space
    msg_col = 2 + ts_width + 1 + len(blk.tag) + 1
    prefix_len = 2 + ts_width
    for indent, text in blk.cont:
        pad = " " * max(1, msg_col + indent - prefix_len)
        out.write(f"{blk.level} {dots}{pad}{text}\n")

    for text in blk.trailer:
        out.write(text + "\n")


def emit_memory_row(out, idx, t, vals, args, cols, widths, stats):
    ts = fmt_timestamp(t, args.time_format)
    fields = []
    for c, w in zip(cols, widths):
        gmax, gidx, maxima, minima = stats[c]
        field = fmt_size_fixed(vals[c], args.unit, args.decimals).rjust(w)
        if args.percent:
            pct = (vals[c] / gmax * 100.0) if gmax else 0.0
            field += f" [{pct:>{pct_field_width(args.pct_decimals) - 3}.{args.pct_decimals}f}%]"
        if args.markers:
            if idx == gidx:
                mark = args.mark_global_max
            elif idx in maxima:
                mark = args.mark_local_max
            elif idx in minima:
                mark = args.mark_local_min
            else:
                mark = ""
            field += " " + mark.ljust(args.mark_width)
        fields.append(field)
    out.write(f"M {ts} {args.sep.join(fields)}\n".rstrip() + "\n")


def emit_memory_header(out, args, cols, widths, ts_width):
    fields = []
    for c, w in zip(cols, widths):
        extra = 0
        if args.percent:
            extra += 1 + pct_field_width(args.pct_decimals)
        if args.markers:
            extra += 1 + args.mark_width
        fields.append(c.rjust(w) + " " * extra)
    out.write(f"M {' ' * ts_width} {args.sep.join(fields)}\n".rstrip() + "\n")


# --------------------------------------------------------------------------
# main
# --------------------------------------------------------------------------

def main():
    p = argparse.ArgumentParser(
        description="Merge a mkdwarfs console log with a memory usage TSV log.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument("console", help="mkdwarfs console log")
    p.add_argument("memory", nargs="?", help="memory log in TSV format")
    p.add_argument("-o", "--output", help="write to file instead of stdout")

    g = p.add_argument_group("timestamps")
    g.add_argument("-t", "--time-format", choices=["hms", "seconds"],
                   default="hms",
                   help="'hms' for 00:00:00.000000, 'seconds' for '    0.000000'")
    g.add_argument("--memory-offset", type=float, default=0.0, metavar="SEC",
                   help="shift memory log times by this many seconds")
    g.add_argument("--sort-console", action="store_true",
                   help="stably sort console statements by timestamp; the raw "
                        "log can be a few microseconds out of order because "
                        "several threads log concurrently")

    g = p.add_argument_group("memory columns")
    g.add_argument("-c", "--columns", default="anon,allocated",
                   help="comma separated list of memory columns to keep "
                        "('all' for every column)")
    g.add_argument("-u", "--unit", choices=["MiB", "GiB"], default="GiB",
                   help="unit for the memory log figures")
    g.add_argument("-d", "--decimals", type=int, default=3,
                   help="number of decimals for the memory log figures")
    g.add_argument("--sep", default="  ", help="separator between memory columns")
    g.add_argument("--no-column-header", dest="column_header",
                   action="store_false", help="do not emit the column title line")

    g = p.add_argument_group("percentages and extrema markers")
    g.add_argument("--no-percent", dest="percent", action="store_false",
                   help="do not show each value as a percentage of the "
                        "column maximum")
    g.add_argument("--pct-decimals", type=int, default=1,
                   help="number of decimals for the percentages")
    g.add_argument("--no-markers", dest="markers", action="store_false",
                   help="do not mark maxima and minima")
    g.add_argument("-p", "--prominence", type=float, default=2.0, metavar="PCT",
                   help="minimum prominence of a local extremum, in percent of "
                        "the column maximum; larger values mark fewer, more "
                        "significant extrema")
    g.add_argument("--mark-global-max", default="***", metavar="STR",
                   help="marker for the global maximum")
    g.add_argument("--mark-local-max", default="+++", metavar="STR",
                   help="marker for a local maximum")
    g.add_argument("--mark-local-min", default="---", metavar="STR",
                   help="marker for a local minimum")

    g = p.add_argument_group("aggregation")
    g.add_argument("-a", "--aggregate", action="append", metavar="SUBSTRING",
                   help="console sections to aggregate (repeatable); "
                        f"default: {DEFAULT_AGGREGATE_SECTIONS}")
    g.add_argument("--aggregate-all", action="store_true",
                   help="aggregate every multi-line section that has size figures")
    g.add_argument("--no-aggregate", action="store_true",
                   help="disable aggregation entirely")
    g.add_argument("--agg-unit", choices=["auto", "MiB", "GiB"], default="auto",
                   help="unit for the aggregate figures ('auto' = mkdwarfs style)")
    g.add_argument("--agg-count", action="store_true",
                   help="also show how many figures went into the total")
    g.add_argument("--no-capacity", dest="capacity", action="store_false",
                   help="do not total the '(capacity: ...)' figures")
    g.add_argument("--no-capacity-percent", dest="capacity_percent",
                   action="store_false",
                   help="do not show the total as a percentage of the capacity")

    args = p.parse_args()

    if args.aggregate is None:
        args.aggregate = DEFAULT_AGGREGATE_SECTIONS
    if args.no_aggregate:
        args.aggregate = []
        args.aggregate_all = False
    if args.decimals < 0:
        p.error("--decimals must not be negative")
    if args.pct_decimals < 0:
        p.error("--pct-decimals must not be negative")
    args.mark_width = max(len(args.mark_global_max), len(args.mark_local_max),
                          len(args.mark_local_min))

    blocks, prologue = parse_console(args.console)
    if not blocks:
        sys.exit(f"{args.console}: no timestamped log lines found")
    times = unwrap_midnight(blocks)
    t0 = times[0]
    times = [t - t0 for t in times]

    if args.sort_console:
        order = sorted(range(len(blocks)), key=lambda i: times[i])
        blocks = [blocks[i] for i in order]
        times = [times[i] for i in order]

    mem_cols, mem_rows = [], []
    if args.memory:
        all_cols, mem_rows = parse_memory(args.memory)
        if args.columns.strip().lower() == "all":
            mem_cols = all_cols
        else:
            mem_cols = [c.strip() for c in args.columns.split(",") if c.strip()]
            unknown = [c for c in mem_cols if c not in all_cols]
            if unknown:
                sys.exit(f"{args.memory}: unknown column(s) {', '.join(unknown)}; "
                         f"available: {', '.join(all_cols)}")

    ts_width = timestamp_width(args.time_format)
    dots = "." * ts_width
    val_width = size_field_width(args.unit, args.decimals)
    widths = [max(val_width, len(c)) for c in mem_cols]
    stats = analyze_columns(mem_rows, mem_cols, args.prominence)

    out = open(args.output, "w", encoding="utf-8") if args.output else sys.stdout
    try:
        for line in prologue:
            out.write(line + "\n")

        if mem_cols and args.column_header:
            emit_memory_header(out, args, mem_cols, widths, ts_width)

        # merge: walk the console blocks and flush memory samples that are due
        mem_times = [t + args.memory_offset for t, _ in mem_rows]
        mi = 0
        for blk, t in zip(blocks, times):
            while mi < len(mem_times) and mem_times[mi] <= t:
                emit_memory_row(out, mi, mem_times[mi], mem_rows[mi][1],
                                args, mem_cols, widths, stats)
                mi += 1
            emit_console_block(out, blk, t, args, ts_width, dots)
        while mi < len(mem_times):
            emit_memory_row(out, mi, mem_times[mi], mem_rows[mi][1],
                            args, mem_cols, widths, stats)
            mi += 1
    finally:
        if args.output:
            out.close()


if __name__ == "__main__":
    main()
