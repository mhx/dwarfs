#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
# SPDX-License-Identifier: MIT

"""
Aggregate and de-duplicate compiler warnings from all jobs in a GitHub Actions run.

Examples:
  python gh_warnings.py 23166081750
  python gh_warnings.py https://github.com/mhx/dwarfs/actions/runs/23166081750

Caching:
  By default, job logs are cached under:
    .gh-actions-log-cache/<owner>/<repo>/<run_id>/<job_id>.log

Environment:
  GH_TOKEN or GITHUB_TOKEN (optional, but recommended)
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import re
import sys
import urllib.error
import urllib.parse
import urllib.request

DEFAULT_REPO = "mhx/dwarfs"
API_ROOT = "https://api.github.com"
USER_AGENT = "gh-actions-warning-report/1.0"

RUN_URL_RE = re.compile(
    r"^https?://github\.com/(?P<owner>[^/]+)/(?P<repo>[^/]+)/actions/runs/(?P<run_id>\d+)(?:/.*)?$"
)
ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[ -/]*[@-~]")
TIMESTAMP_RE = re.compile(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?Z\s+")
WHITESPACE_RE = re.compile(r"\s+")
SUMMARY_RE = re.compile(
    r"^(?:\d+\s+warnings?\s+generated\.|warning:\s+\d+\s+warnings?\s+generated\.)$",
    re.IGNORECASE,
)

# GCC / Clang / AppleClang:
#   path:line:col: warning: message [-Wfoo]
#   path:line: warning: message [-Wfoo]
# Windows compiler paths may start with a drive letter, so handle those first.
GNU_WIN_WARNING_RE = re.compile(
    r"^(?P<path>[A-Za-z]:.*?)(?::(?P<line>\d+))(?::(?P<col>\d+))?:\s+warning:\s+(?P<msg>.+?)\s*$"
)
GNU_WARNING_RE = re.compile(
    r"^(?P<path>.+?):(?P<line>\d+)(?::(?P<col>\d+))?:\s+warning:\s+(?P<msg>.+?)\s*$"
)

# MSVC file warnings:
#   path(line): warning C4996: ...
#   path(line,col): warning C4100: ...
MSVC_FILE_WARNING_RE = re.compile(
    r"^(?P<path>.+?)\((?P<line>\d+)(?:,(?P<col>\d+))?\)\s*:\s*warning\s+"
    r"(?P<code>[A-Z]+\d+)\s*:\s*(?P<msg>.+?)\s*$",
    re.IGNORECASE,
)

# MSVC tool warnings:
#   cl : Command line warning D9025 : ...
#   LINK : warning LNK4099: ...
MSVC_TOOL_WARNING_RE = re.compile(
    r"^(?P<tool>[A-Za-z0-9_.+\-\\/ ]+?)\s*:\s*"
    r"(?:(?P<kind>command line)\s+)?warning\s+"
    r"(?P<code>[A-Z]+\d+)\s*:\s*(?P<msg>.+?)\s*$",
    re.IGNORECASE,
)


class NoRedirectHandler(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


class GitHubClient:
    def __init__(self, token: str | None = None, api_version: str | None = None) -> None:
        self.token = token
        self.api_version = api_version
        self.no_redirect_opener = urllib.request.build_opener(NoRedirectHandler())

    def _headers(self, include_auth: bool = True) -> dict[str, str]:
        headers = {
            "Accept": "application/vnd.github+json",
            "User-Agent": USER_AGENT,
        }
        if self.api_version:
            headers["X-GitHub-Api-Version"] = self.api_version
        if include_auth and self.token:
            headers["Authorization"] = f"Bearer {self.token}"
        return headers

    def _request(
        self,
        url: str,
        *,
        no_redirect: bool = False,
        include_auth: bool = True,
    ) -> urllib.response.addinfourl:
        req = urllib.request.Request(url, headers=self._headers(include_auth=include_auth))
        if no_redirect:
            return self.no_redirect_opener.open(req, timeout=60)
        return urllib.request.urlopen(req, timeout=60)

    def get_json(self, url: str) -> dict:
        try:
            with self._request(url) as resp:
                return json.loads(resp.read().decode("utf-8"))
        except urllib.error.HTTPError as e:
            body = e.read().decode("utf-8", "replace")
            raise SystemExit(f"GitHub API error {e.code} for {url}: {body}") from e

    def paged_get(self, url: str, list_key: str) -> list[dict]:
        items: list[dict] = []
        page = 1
        while True:
            query = urllib.parse.urlencode({"per_page": 100, "page": page})
            page_url = f"{url}?{query}"
            data = self.get_json(page_url)
            page_items = data.get(list_key, [])
            if not isinstance(page_items, list):
                raise SystemExit(f"Expected list under key {list_key!r}, got: {type(page_items)!r}")
            items.extend(page_items)
            if len(page_items) < 100:
                return items
            page += 1

    def download_job_log(self, owner: str, repo: str, job_id: int, dest: pathlib.Path) -> bool:
        api_url = f"{API_ROOT}/repos/{owner}/{repo}/actions/jobs/{job_id}/logs"

        try:
            with self._request(api_url, no_redirect=True) as resp:
                data = resp.read()
                dest.write_bytes(data)
                return True
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return False
            if e.code != 302:
                body = e.read().decode("utf-8", "replace")
                raise SystemExit(
                    f"Failed to download logs for job {job_id}: {e.code} {body}"
                ) from e
            location = e.headers.get("Location")
            if not location:
                raise SystemExit(f"GitHub returned 302 for job {job_id} without a Location header")

        # The redirect target is a short-lived download URL.
        # Fetch it without the GitHub Authorization header.
        try:
            with self._request(location, include_auth=False) as resp:
                dest.write_bytes(resp.read())
                return True
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return False
            body = e.read().decode("utf-8", "replace")
            raise SystemExit(
                f"Failed to fetch redirected log download for job {job_id}: {e.code} {body}"
            ) from e


def parse_run_ref(value: str) -> tuple[str, str, int]:
    m = RUN_URL_RE.match(value)
    if m:
        return m.group("owner"), m.group("repo"), int(m.group("run_id"))
    if value.isdigit():
        owner, repo = DEFAULT_REPO.split("/", 1)
        return owner, repo, int(value)
    raise SystemExit("Expected a GitHub Actions run URL or a numeric run ID.")


def strip_ansi(s: str) -> str:
    return ANSI_RE.sub("", s)


def strip_log_prefix(line: str) -> str:
    line = line.rstrip("\r\n")
    line = strip_ansi(line)
    line = TIMESTAMP_RE.sub("", line)
    return line.strip()


def normalize_path(path: str, repo_name: str) -> str:
    path = path.strip().strip('"').strip("'")
    path = path.replace("\\", "/")
    path = re.sub(r"^[A-Za-z]:", "", path)

    marker = f"/{repo_name}/{repo_name}/"
    idx = path.find(marker)
    if idx != -1:
        return path[idx + len(marker):].lstrip("/")

    return path.lstrip("/")


def normalize_message(msg: str) -> str:
    msg = strip_ansi(msg)
    msg = WHITESPACE_RE.sub(" ", msg).strip()

    # Normalize common workspace paths that sometimes leak into the diagnostic body.
    msg = re.sub(r"[A-Za-z]:[/\\]a[/\\][^/\\]+[/\\][^/\\]+[/\\]", "", msg)
    msg = re.sub(r"/(?:Users|home)/runner/work/[^/]+/[^/]+/", "", msg)

    return msg


def warning_from_line(line: str, repo_name: str) -> dict[str, str] | None:
    if not line or SUMMARY_RE.match(line):
        return None

    m = GNU_WIN_WARNING_RE.match(line) or GNU_WARNING_RE.match(line)
    if m:
        path = normalize_path(m.group("path"), repo_name)
        line_no = m.group("line")
        col_no = m.group("col") or ""
        msg = normalize_message(m.group("msg"))
        return {
            "kind": "gnu",
            "display": f"{path}:{line_no}" + (f":{col_no}" if col_no else "") + f": warning: {msg}",
            "key": f"gnu|{path}|{line_no}|{col_no}|{msg}",
        }

    m = MSVC_FILE_WARNING_RE.match(line)
    if m:
        path = normalize_path(m.group("path"), repo_name)
        line_no = m.group("line")
        col_no = m.group("col") or ""
        code = m.group("code").upper()
        msg = normalize_message(m.group("msg"))
        display = f"{path}({line_no}" + (f",{col_no}" if col_no else "") + f"): warning {code}: {msg}"
        return {
            "kind": "msvc-file",
            "display": display,
            "key": f"msvc-file|{path}|{line_no}|{col_no}|{code}|{msg}",
        }

    m = MSVC_TOOL_WARNING_RE.match(line)
    if m:
        tool = WHITESPACE_RE.sub(" ", m.group("tool")).strip()
        kind = (m.group("kind") or "").strip().lower()
        code = m.group("code").upper()
        msg = normalize_message(m.group("msg"))
        prefix = f"{tool}: "
        if kind:
            prefix += f"{kind} "
        display = f"{prefix}warning {code}: {msg}"
        return {
            "kind": "msvc-tool",
            "display": display,
            "key": f"msvc-tool|{tool.lower()}|{kind}|{code}|{msg}",
        }

    return None


def parse_warnings(log_text: str, repo_name: str) -> list[dict[str, str]]:
    warnings: list[dict[str, str]] = []
    seen_in_file: set[str] = set()

    for raw_line in log_text.splitlines():
        line = strip_log_prefix(raw_line)
        warning = warning_from_line(line, repo_name)
        if not warning:
            continue
        if warning["key"] in seen_in_file:
            continue
        seen_in_file.add(warning["key"])
        warnings.append(warning)

    return warnings


def human_output(run_url: str, scanned_jobs: list[str], aggregated: dict[str, dict]) -> str:
    lines: list[str] = []
    lines.append(f"Run: {run_url}")
    lines.append(f"Jobs scanned: {len(scanned_jobs)}")
    lines.append(f"Unique warnings: {len(aggregated)}")
    lines.append("")

    for idx, entry in enumerate(
        sorted(
            aggregated.values(),
            key=lambda x: (
                x["display"].lower(),
                sorted(x["jobs"]),
            ),
        ),
        start=1,
    ):
        job_list = sorted(entry["jobs"])
        lines.append(f"{idx}. {entry['display']}")
        for job in job_list:
            lines.append(f"   - {job}")
        lines.append("")

    if len(lines) == 4:
        lines.append("No compiler warnings found.")
    return "\n".join(lines).rstrip() + "\n"


def json_output(run_url: str, scanned_jobs: list[str], aggregated: dict[str, dict]) -> str:
    payload = {
        "run_url": run_url,
        "jobs_scanned": len(scanned_jobs),
        "scanned_job_names": scanned_jobs,
        "unique_warnings": len(aggregated),
        "warnings": [
            {
                "warning": entry["display"],
                "jobs": sorted(entry["jobs"]),
            }
            for entry in sorted(aggregated.values(), key=lambda x: x["display"].lower())
        ],
    }
    return json.dumps(payload, indent=2, sort_keys=False) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Download GitHub Actions job logs for a run and aggregate compiler warnings."
    )
    parser.add_argument("run", help="Run URL or numeric run ID")
    parser.add_argument(
        "--token",
        default=os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN"),
        help="GitHub token (defaults to GH_TOKEN or GITHUB_TOKEN)",
    )
    parser.add_argument(
        "--api-version",
        default=None,
        help="Optional X-GitHub-Api-Version header value",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Emit JSON instead of human-readable text",
    )
    parser.add_argument(
        "--cache-dir",
        default=".gh-actions-log-cache",
        help="Directory for cached job logs (default: .gh-actions-log-cache)",
    )
    parser.add_argument(
        "--refresh",
        action="store_true",
        help="Force re-download even when a cached job log already exists",
    )
    args = parser.parse_args()

    owner, repo, run_id = parse_run_ref(args.run)
    repo_name = repo

    gh = GitHubClient(token=args.token, api_version=args.api_version)

    run = gh.get_json(f"{API_ROOT}/repos/{owner}/{repo}/actions/runs/{run_id}")
    attempt = run.get("run_attempt")
    run_url = run.get("html_url") or f"https://github.com/{owner}/{repo}/actions/runs/{run_id}"

    if attempt is None:
        raise SystemExit(f"Could not read run_attempt from run {run_id}")

    jobs = gh.paged_get(
        f"{API_ROOT}/repos/{owner}/{repo}/actions/runs/{run_id}/attempts/{attempt}/jobs",
        "jobs",
    )

    cache_root = pathlib.Path(args.cache_dir)
    log_dir = cache_root / owner / repo / str(run_id)
    log_dir.mkdir(parents=True, exist_ok=True)

    aggregated: dict[str, dict] = {}
    scanned_jobs: list[str] = []

    for job in jobs:
        job_id = int(job["id"])
        job_name = str(job.get("name") or job_id)
        log_path = log_dir / f"{job_id}.log"

        conclusion = str(job.get("conclusion") or "").lower()
        status = str(job.get("status") or "").lower()

        if conclusion == "skipped":
            print(f"Skipping job without logs: {job_name} (job_id={job_id}, conclusion=skipped)", file=sys.stderr)
            continue

        if args.refresh or not log_path.exists() or log_path.stat().st_size == 0:
            downloaded = gh.download_job_log(owner, repo, job_id, log_path)
            if not downloaded:
                reason = f"status={status or 'unknown'}, conclusion={conclusion or 'unknown'}"
                print(f"Skipping job without downloadable logs: {job_name} (job_id={job_id}, {reason})", file=sys.stderr)
                continue

        if not log_path.exists() or log_path.stat().st_size == 0:
            print(f"Skipping empty cached log: {job_name} (job_id={job_id})", file=sys.stderr)
            continue

        scanned_jobs.append(job_name)
        text = log_path.read_text(encoding="utf-8", errors="replace")

        for warning in parse_warnings(text, repo_name):
            entry = aggregated.setdefault(
                warning["key"],
                {
                    "display": warning["display"],
                    "jobs": set(),
                },
            )
            entry["jobs"].add(job_name)

    output = json_output(run_url, scanned_jobs, aggregated) if args.json else human_output(run_url, scanned_jobs, aggregated)
    sys.stdout.write(output)


if __name__ == "__main__":
    main()
