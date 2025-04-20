#!/usr/bin/env python3

import json
import re
import os
import subprocess
import argparse
from datetime import datetime


def parse_iso8601(ts: str) -> datetime:
    """
    Parse an ISO8601 timestamp, stripping a trailing 'Z' if present.
    """
    if ts.endswith("Z"):
        ts = ts[:-1]
    return datetime.fromisoformat(ts)


def main():
    parser = argparse.ArgumentParser(
        description="Convert GitHub Actions job+step timings into a Chrome tracing JSON"
    )
    parser.add_argument(
        "-i",
        "--input",
        help="Path to the input JSON from `gh api /actions/runs/.../jobs`",
    )
    parser.add_argument(
        "-j",
        "--job",
        help="GitHub Actions job id to trace",
    )
    parser.add_argument(
        "-o",
        "--output",
        default="trace.json",
        help="Path to write the Chrome tracing JSON",
    )
    parser.add_argument(
        "-n",
        "--ninja",
        action="store_true",
        help="Include ninja build logs in the trace",
    )
    parser.add_argument(
        "--ninja-log-base",
        default="/mnt/opensource/artifacts/dwarfs/ninja-logs",
        help="Base path for ninja logs",
    )
    parser.add_argument(
        "--ninjatracing",
        default="/home/mhx/git/github/ninjatracing/ninjatracing",
        help="Path to the ninjatracing tool",
    )
    args = parser.parse_args()

    if args.input is None and args.job is None:
        parser.print_help()
        exit(1)

    if args.input:
        # Load the GitHub API output
        with open(args.input, "r") as f:
            data = json.load(f)
    else:
        result = subprocess.run([
            "gh",
            "api",
            "--paginate",
            f"/repos/mhx/dwarfs/actions/runs/{args.job}/jobs",
        ], check=True, capture_output=True)
        data = json.loads(result.stdout)

    # The API may nest under 'jobs'
    jobs = data.get("jobs", data)
    events = []

    job_events = 0

    for job in jobs:
        # Job-level event
        if job.get("started_at") and job.get("completed_at"):
            job_id = job.get("id", 0)
            name = job.get("name", f"job-{job_id}")
            runner_id = job.get("runner_name", f"runner-{job_id}")

            start = parse_iso8601(job["started_at"])
            end = parse_iso8601(job["completed_at"])
            ts = int(start.timestamp() * 1e6)
            dur = int((end - start).total_seconds() * 1e6)

            if dur > 0:
                job_events += 1
                events.append(
                    {
                        "name": name,
                        "cat": "job",
                        "ph": "X",
                        "ts": ts,
                        "dur": dur,
                        "pid": runner_id,
                        "tid": 0,
                    }
                )

                # Step-level events
                for step in job.get("steps", []):
                    if step.get("started_at") and step.get("completed_at"):
                        s = parse_iso8601(step["started_at"])
                        e = parse_iso8601(step["completed_at"])
                        ts_s = int(s.timestamp() * 1e6)
                        dur_s = int((e - s).total_seconds() * 1e6)
                        if dur_s > 0:
                            events.append(
                                {
                                    "name": step.get("name"),
                                    "cat": "step",
                                    "ph": "X",
                                    "ts": ts_s,
                                    "dur": dur_s,
                                    "pid": runner_id,
                                    "tid": 0,
                                }
                            )

                            if args.ninja and step.get("name") == "Run Build":
                                run_id = job.get("run_id", 0)
                                # get arch, dist, config from "linux (arm64v8, alpine, clang-release-ninja-static) / docker-build"
                                m = re.match(r"linux \(([^,]+), ([^,]+), ([^,]+)\)", name)
                                if m:
                                    log_path = f"{args.ninja_log_base}/{run_id}/{m.group(1)},{m.group(2)},{m.group(3)}.log"
                                    if os.path.exists(log_path):
                                        # Read output from ninjatracing tool
                                        result = subprocess.run(
                                            [args.ninjatracing, log_path],
                                            capture_output=True,
                                            text=True,
                                            check=True,
                                        )
                                        # Parse JSON output
                                        ninja_json = json.loads(result.stdout)
                                        # Add events to the trace
                                        for event in ninja_json:
                                            if event.get("dur") > 0:
                                                # Adjust the timestamp to match the step
                                                event["ts"] += ts_s
                                                event["pid"] = runner_id
                                                events.append(event)
                                    else:
                                        print(f"Log file not found: {log_path}")
            else:
                print(
                    f"Job {name} has zero duration: {job['started_at']} -> {job['completed_at']}"
                )

    # Compose the trace
    trace = {"traceEvents": events}

    # Write out
    with open(args.output, "w") as f:
        json.dump(trace, f, indent=2)

    print(f"Wrote {len(events)} events ({job_events} job events) to {args.output}")


if __name__ == "__main__":
    main()
