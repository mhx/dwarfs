#!/usr/bin/env python3

import argparse
import os
import json
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.container as mplc

from packaging.version import Version
from tinydb import TinyDB, Query
from tqdm import tqdm


TABLE_NAME = "benchmarks"


def import_data(db_path, *args):
    """
    Import all JSON files in 'json_dir' into the TinyDB database at 'db_path'.
    """
    db = TinyDB(db_path)
    table = db.table(TABLE_NAME)

    files = []

    for directory in args:
        files.extend(
            [
                os.path.join(directory, f)
                for f in os.listdir(directory)
                if f.endswith(".json")
            ]
        )

    dbtimes = {doc['time'] for doc in table.all()}
    skipped = 0

    for path in tqdm(files, desc="Importing JSON files", unit="file"):
        with open(path, "r") as f:
            data = json.load(f)
            if data["time"] in dbtimes:
                skipped += 1
                continue
            if "exit_codes" in data:
                del data["times"]
                del data["exit_codes"]
            table.insert(data)

    print(f"Skipped {skipped} files that were already in the database.")


def query_and_bar_chart(db_path, arch, version, binary_type):
    """
    Example query that:
      - filters records by arch and version
      - groups them by (name, config)
      - plots a grouped bar chart of 'mean' times (with error bars using 'stddev').
    """
    db = TinyDB(db_path)
    table = db.table(TABLE_NAME)
    Q = Query()

    release_configs = {
        None,
        "clang",
        "clang-minsize-musl-lto",
    }

    # Fetch rows that match the arch, version, binary_type etc.
    rows = table.search(
        (Q.arch == arch)
        &
        # (Q.version == version) &
        (Q.config.test(lambda x: x in release_configs))
        & (Q.type == binary_type)
        &
        # (Q.name.test(lambda x: not x.endswith("_mmap"))) &
        (Q.mean.exists())
        & (Q.stddev.exists())
    )

    # Sort and create a DataFrame with the relevant columns.
    # rows.sort(key=lambda r: (r.get("name"), r.get("config")))
    # df = pd.DataFrame(rows, columns=["name", "config", "mean", "stddev"])
    # rows.sort(key=lambda r: (r.get("name"), r.get("commit_time")))
    df = pd.DataFrame(rows, columns=["name", "version", "mean", "stddev"])
    df["version_object"] = df["version"].apply(Version)

    # Pivot the DataFrame so that "name" becomes the row index and different "config"
    # values become separate columns for the "mean" and "stddev" values.
    df_pivot = df.pivot(
        index="name", columns=["version_object"], values=["mean", "stddev"]
    )

    # Extract the DataFrame for means and stddevs.
    mean_df = df_pivot["mean"]
    stddev_df = df_pivot["stddev"]

    # === Plotting ===

    # Plot the normalized horizontal grouped bar chart.
    ax = mean_df.plot(kind="bar", yerr=stddev_df, capsize=3, figsize=(10, 10), log=True, width=0.8)

    for container in ax.containers:
        if isinstance(container, mplc.BarContainer):
            pass
        if isinstance(container, mplc.ErrorbarContainer):
            # Set the error bar color to match the bar color.
            for errbar in container.lines[1]:
                errbar.set_alpha(0.25)
            for errbar in container.lines[2]:
                errbar.set_alpha(0.25)

    # Customize titles, labels, and legend.
    ax.set_title(f"Benchmark results for arch={arch}, type={binary_type}")
    # Rotate x-axis labels for better readability.
    ax.set_xticklabels(ax.get_xticklabels(), rotation=20, ha="right")
    ax.set_ylabel("Benchmark Time")
    ax.set_xlabel("Benchmark Name")
    # Add grid lines in the background
    ax.set_axisbelow(True)
    ax.grid(axis="y", linestyle="-", alpha=0.5, which="both")
    # Set y-axis range (1ms to 100s)
    ax.set_ylim(0.001, 100.0)

    # Put the legend outside the plot area.
    ax.legend(
        title="Version", bbox_to_anchor=(1.005, 1), loc="upper left", frameon=False
    )

    for boundary in np.arange(len(df)) - 0.5:
        plt.axvline(x=boundary, color='grey', linestyle='-', linewidth=0.5)

    # Maximize the space for the figure.
    plt.subplots_adjust(left=0.06, right=0.94, top=0.97, bottom=0.12)

    # ax.legend(title="Version")

    # plt.tight_layout()
    plt.show()


# Interesting queries:


def main():
    parser = argparse.ArgumentParser(
        description="Import JSON benchmarks into TinyDB and generate charts."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    # Sub-command: import
    import_parser = subparsers.add_parser(
        "import", help="Import JSON files into TinyDB."
    )
    import_parser.add_argument("db_path", help="TinyDB database file.")
    import_parser.add_argument(
        "json_dirs", nargs="+", help="Directories containing JSON files to import."
    )

    # Sub-command: bar
    bar_parser = subparsers.add_parser("bar", help="Generate a grouped bar chart.")
    bar_parser.add_argument("db_path", help="TinyDB database file.")
    bar_parser.add_argument("--arch", required=True, help="Architecture to filter by.")
    bar_parser.add_argument("--version", required=True, help="Version to filter by.")
    bar_parser.add_argument(
        "--binary", default="standalone", help="Version to filter by."
    )

    # Sub-command: line
    line_parser = subparsers.add_parser("line", help="Generate a line chart over time.")
    line_parser.add_argument("db_path", help="TinyDB database file.")
    line_parser.add_argument("--arch", required=True, help="Architecture to filter by.")
    line_parser.add_argument("--config", required=True, help="Config to filter by.")
    line_parser.add_argument(
        "--binary", default="standalone", help="Version to filter by."
    )

    args = parser.parse_args()

    if args.command == "import":
        import_data(args.db_path, *args.json_dirs)
    elif args.command == "bar":
        query_and_bar_chart(args.db_path, args.arch, args.version, args.binary)
    elif args.command == "line":
        query_and_line_chart(args.db_path, args.arch, args.config, args.binary)


if __name__ == "__main__":
    main()
