#!/usr/bin/env python3
from math import ceil

def _nice_max(vmax: float) -> float:
    if vmax <= 0:
        return 1.0
    # choose a rounded "nice" upper bound
    magnitude = 10 ** max(0, len(str(int(vmax))) - 1)
    candidates = [1, 2, 5, 10]
    base = vmax / magnitude
    for c in candidates:
        if base <= c:
            return c * magnitude
    return 10 * magnitude

def _estimate_label_col_width(categories, font_px=12, char_px=7, line_gap_px=2, padding_px=12):
    """
    Estimate width needed for multi-line, right-aligned y labels.
    Monospace => char_px ~7 at 12px is a good on-screen heuristic.
    """
    max_px = 0
    for cat in categories:
        lines = str(cat).split("\n")
        widest = max(len(s) for s in lines) if lines else 0
        block_height = len(lines) * (font_px + line_gap_px)
        # width based on widest line; height not needed for width calc
        max_px = max(max_px, widest * char_px)
    return max_px + padding_px  # padding between labels and axis

def bar_chart_svg_horizontal(
    title: str,
    categories: list[str],
    values: list[float],
    units: str = "",
    width: int = 800,
    height: int = None,
    margin: tuple[int, int, int, int] | None = None,  # top,right,bottom,left (left auto if None)
    tick_count: int = 5,
    decimals: int = 2,
    show_values: bool = True,
):
    assert len(categories) == len(values), "categories and values must match"
    W, H = width, height

    # Layout: dynamic left margin to fit multi-line labels
    label_col = _estimate_label_col_width(categories)
    if margin is None:
        mt, mr, mb, ml = 48, 28, 44, int(32 + label_col)  # baseline + labels
    else:
        mt, mr, mb, ml = margin
        ml = max(ml, int(12 + label_col))  # ensure labels fit

    n = max(1, len(values))
    bar_gap = 0.3

    if H is None:
        H = int(mt + mb + 32 * n / (1 - bar_gap))

    cw, ch = W - ml - mr, H - mt - mb
    bh = ch / n * (1 - bar_gap)
    gap = ch / n * bar_gap

    vmax = max([v if v is not None else 0 for v in values]) if values else 1.0
    x_max = _nice_max(vmax)
    x0, y0 = ml, mt + ch  # chart origin (bottom-left)

    # SVG & CSS
    out = []
    out.append(f'''<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" role="img" aria-label="{title}">
  <style>
    :root {{
      color: var(--fgColor-default, #24292f);
      font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
    }}
    @media (prefers-color-scheme: dark) {{
      :root {{ color: var(--fgColor-default, #c9d1d9); }}
    }}
    .t {{ fill: currentColor; font-size: 14px; dominant-baseline: middle; }}
    .title {{ font-weight: 600; font-size: 18px; dominant-baseline: hanging; }}
    .axis {{ stroke: currentColor; stroke-width: 1; }}
    .grid {{ stroke: currentColor; stroke-width: 1; opacity: .2; }}
    .bar {{ fill: currentColor; opacity: .8; }}
    .unit {{ opacity: .8; font-size: 14px; }}
  </style>
  <rect x="0" y="0" width="{W}" height="{H}" fill="none"/>
  <text x="{ml}" y="{mt-28}" class="t title">{title}</text>
''')

    # Axes (x axis along bottom, y axis at left of plot area)
    out.append(f'  <line x1="{x0}" y1="{y0}" x2="{x0+cw}" y2="{y0}" class="axis"/>')
    out.append(f'  <line x1="{x0}" y1="{mt}" x2="{x0}" y2="{y0}" class="axis"/>')

    # X grid + ticks + labels
    for i in range(tick_count + 1):
        x = x0 + cw * (i / tick_count)
        val = x_max * (i / tick_count)
        out.append(f'  <line x1="{x:.2f}" y1="{mt}" x2="{x:.2f}" y2="{y0}" class="grid"/>')
        out.append(f'  <text x="{x:.2f}" y="{y0+16}" class="t" text-anchor="middle">{val:.{decimals}f}</text>')

    # Unit label (x)
    if units:
        out.append(f'  <text x="{x0+cw}" y="{y0+30}" class="t unit" text-anchor="end">{units}</text>')

    # Bars + Y labels (right-aligned, multi-line)
    y = mt + gap / 2
    for cat, v in zip(categories, values):
        # bar width proportional to value
        bw = 0 if x_max == 0 or v is None else cw * (v / x_max)
        # bar rect
        out.append(f'  <rect class="bar" x="{x0:.2f}" y="{y:.2f}" width="{bw:.2f}" height="{bh:.2f}" rx="3" ry="3"/>')

        # category label block, right-aligned at (x0 - 10)
        label_x = x0 - 10
        lines = str(cat).split("\n")
        line_height = 14  # px
        block_total = len(lines) * line_height
        base_y = y + (bh - block_total) / 2 + line_height / 2
        for idx, s in enumerate(lines):
            ly = base_y + idx * line_height
            out.append(f'  <text x="{label_x:.2f}" y="{ly:.2f}" class="t" text-anchor="end">{s}</text>')

        # value label: inside the bar if there's room; otherwise outside at end
        if show_values:
            text = '❌' if v is None else f'{v:.{decimals}f}'
            ## tx_inside = x0 + bw - 6
            tx_out = x0 + bw + 6
            ty = y + bh / 2
            ## # If bar is wide enough to fit ~chwidth*len(text), put it inside, else outside
            ## approx_text_px = 7 * len(text) + 2
            ## if bw >= approx_text_px + 8:
            ##     out.append(f'  <text x="{tx_inside:.2f}" y="{ty:.2f}" class="t" text-anchor="end">{text}</text>')
            ## else:
            ##     out.append(f'  <text x="{tx_out:.2f}" y="{ty:.2f}" class="t">{text}</text>')
            out.append(f'  <text x="{tx_out:.2f}" y="{ty:.2f}" class="t">{text}</text>')

        y += bh + gap

    out.append('</svg>')
    return "\n".join(out)

# --- Example usage ---
if __name__ == "__main__":
    charts = [
        {
            "title": "Perl • Compression speed (higher is better)",
            "categories": [".tar.gz\n(pigz)", "7zip\n(-mx=7)", "DwarFS\n(xz)", "DwarFS\n(zstd)"],
            "values": [48629/299, 48629/1407, 48629/133, 48629/303],
            "decimals": 1,
            "units": "MiB/s",
            "filename": "perl_compression_speed.svg",
        },
        {
            "title": "Perl • Decompression speed (higher is better)",
            "categories": [".tar.gz\n(pigz)", "7zip\n(-mx=7)", "DwarFS\n(xz)", "DwarFS\n(zstd)"],
            "values": [48629/139, 48629/74, 48629/74, 48629/74],
            "decimals": 0,
            "units": "MiB/s",
            "filename": "perl_decompression_speed.svg",
        },
        {
            "title": "Perl • Compression CPU time (lower is better)",
            "categories": [".tar.gz\n(pigz)", "7zip\n(-mx=7)", "DwarFS\n(xz)", "DwarFS\n(zstd)"],
            "values": [107, 305, 31, 50],
            "decimals": 0,
            "units": "minutes",
            "filename": "perl_compression_cpu_time.svg",
        },
        {
            "title": "Perl • Decompression CPU time (lower is better)",
            "categories": [".tar.gz\n(pigz)", "7zip\n(-mx=7)", "DwarFS\n(xz)", "DwarFS\n(zstd)"],
            "values": [224, 148, 107, 90],
            "decimals": 0,
            "units": "seconds",
            "filename": "perl_decompression_cpu_time.svg",
        },
        {
            "title": "Perl • Compression ratio (higher is better)",
            "categories": [".tar.gz\n(pigz)", "7zip\n(-mx=7)", "DwarFS\n(xz)", "DwarFS\n(zstd)"],
            "values": [47.49/12.17, 47.49/1.219, 47.49/0.310, 47.49/0.352],
            "decimals": 1,
            "units": "",
            "filename": "perl_compression_ratio.svg",
        },
        {
            "title": "Perl • Mount time (lower is better)",
            "categories": [".tar.gz\n(pigz)", "7zip\n(-mx=7)", "DwarFS\n(xz)", "DwarFS\n(zstd)"],
            "values": [127, 3.638, 0.420, 0.009],
            "decimals": 3,
            "units": "seconds",
            "filename": "perl_mount_time.svg",
        },
        {
            "title": "Perl • Random access speed (higher is better)",
            "categories": [".tar.gz\n(pigz)", "7zip\n(-mx=7)", "DwarFS\n(xz)", "DwarFS\n(zstd)"],
            "values": [None, 2642/(5*3600), 2642/4.33, 2642/1.134],
            "decimals": 2,
            "units": "MiB/s",
            "filename": "perl_random_access_speed.svg",
        },
        #########################################################################################
        {
            "title": "DwarFS CI • Compression speed (higher is better)",
            "categories": [".tar.gz\n(pigz)", "7zip\n(-mx=7)", "DwarFS\n(zstd)"],
            "values": [476365/2428, 476365/8506, 476365/4086],
            "decimals": 1,
            "units": "MiB/s",
            "filename": "dwarfsci_compression_speed.svg",
        },
        {
            "title": "DwarFS CI • Decompression speed (higher is better)",
            "categories": [".tar.gz\n(pigz)", "7zip\n(-mx=7)", "DwarFS\n(zstd)"],
            "values": [476365/1577, 476365/495, 476365/441],
            "decimals": 0,
            "units": "MiB/s",
            "filename": "dwarfsci_decompression_speed.svg",
        },
        {
            "title": "DwarFS CI • Compression CPU time (lower is better)",
            "categories": [".tar.gz\n(pigz)", "7zip\n(-mx=7)", "DwarFS\n(zstd)"],
            "values": [835, 2567, 1872],
            "decimals": 0,
            "units": "minutes",
            "filename": "dwarfsci_compression_cpu_time.svg",
        },
        {
            "title": "DwarFS CI • Decompression CPU time (lower is better)",
            "categories": [".tar.gz\n(pigz)", "7zip\n(-mx=7)", "DwarFS\n(zstd)"],
            "values": [2116, 495, 441],
            "decimals": 0,
            "units": "seconds",
            "filename": "dwarfsci_decompression_cpu_time.svg",
        },
        {
            "title": "DwarFS CI • Compression ratio (higher is better)",
            "categories": [".tar.gz\n(pigz)", "7zip\n(-mx=7)", "DwarFS\n(zstd)"],
            "values": [465.2/142.9, 465.2/60.04, 465.2/28.63],
            "decimals": 1,
            "units": "",
            "filename": "dwarfsci_compression_ratio.svg",
        },
        {
            "title": "DwarFS CI • Mount time (lower is better)",
            "categories": [".tar.gz\n(pigz)", "7zip\n(-mx=7)", "DwarFS\n(zstd)"],
            "values": [1338, 10.5, 0.024],
            "decimals": 3,
            "units": "seconds",
            "filename": "dwarfsci_mount_time.svg",
        },
        {
            "title": "DwarFS CI • Random access speed (higher is better)",
            "categories": [".tar.gz\n(pigz)", "7zip\n(-mx=7)", "DwarFS\n(zstd)"],
            "values": [None, None, 2785/0.774],
            "decimals": 2,
            "units": "MiB/s",
            "filename": "dwarfsci_random_access_speed.svg",
        },
        #########################################################################################
        {
            "title": "Sonniss • Compression speed (higher is better)",
            "categories": [".tar.gz\n(pigz)", "7zip\n(-mx=7)", "DwarFS\n(categorize)"],
            "values": [3146/5.34, 3146/381, 3146/3.98],
            "decimals": 1,
            "units": "MiB/s",
            "filename": "sonniss_compression_speed.svg",
        },
        {
            "title": "Sonniss • Decompression speed (higher is better)",
            "categories": [".tar.gz\n(pigz)", "7zip\n(-mx=7)", "DwarFS\n(categorize)"],
            "values": [3146/13.7, 3146/110, 3146/1.32],
            "decimals": 0,
            "units": "MiB/s",
            "filename": "sonniss_decompression_speed.svg",
        },
        {
            "title": "Sonniss • Compression CPU time (lower is better)",
            "categories": [".tar.gz\n(pigz)", "7zip\n(-mx=7)", "DwarFS\n(categorize)"],
            "values": [121, 1154, 29.0],
            "decimals": 0,
            "units": "seconds",
            "filename": "sonniss_compression_cpu_time.svg",
        },
        {
            "title": "Sonniss • Decompression CPU time (lower is better)",
            "categories": [".tar.gz\n(pigz)", "7zip\n(-mx=7)", "DwarFS\n(categorize)"],
            "values": [17.8, 112, 9.15],
            "decimals": 1,
            "units": "seconds",
            "filename": "sonniss_decompression_cpu_time.svg",
        },
        {
            "title": "Sonniss • Compression ratio (higher is better)",
            "categories": [".tar.gz\n(pigz)", "7zip\n(-mx=7)", "DwarFS\n(categorize)"],
            "values": [3.072/2.725, 3.072/2.255, 3.072/1.664],
            "decimals": 2,
            "units": "",
            "filename": "sonniss_compression_ratio.svg",
        },
        {
            "title": "Sonniss • Mount time (lower is better)",
            "categories": [".tar.gz\n(pigz)", "7zip\n(-mx=7)", "DwarFS\n(categorize)"],
            "values": [13.6, 0.014, 0.008],
            "decimals": 3,
            "units": "seconds",
            "filename": "sonniss_mount_time.svg",
        },
        {
            "title": "Sonniss • Random access speed (higher is better)",
            "categories": [".tar.gz\n(pigz)", "7zip\n(-mx=7)", "DwarFS\n(categorize)"],
            "values": [3146/182, 3146/1842, 3146/2.64],
            "decimals": 2,
            "units": "MiB/s",
            "filename": "sonniss_random_access_speed.svg",
        },
    ]

    for chart in charts:
        filename = chart.pop("filename")
        svg = bar_chart_svg_horizontal(**chart)
        with open(filename, "w", encoding="utf-8") as f:
            f.write(svg)
        print(f"Wrote {filename}")
