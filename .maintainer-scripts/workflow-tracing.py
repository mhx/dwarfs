#!/usr/bin/env python3

import json
import argparse
from datetime import datetime


# Use:
#   gh api --paginate /repos/mhx/dwarfs/actions/runs/14559408056/jobs > jobs.json
#   python3 workflow-tracing.py -i jobs.json -o trace.json


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
        default="jobs.json",
        help="Path to the input JSON from `gh api /actions/runs/.../jobs`",
    )
    parser.add_argument(
        "-o",
        "--output",
        default="trace.json",
        help="Path to write the Chrome tracing JSON",
    )
    args = parser.parse_args()

    # Load the GitHub API output
    with open(args.input, "r") as f:
        data = json.load(f)

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
