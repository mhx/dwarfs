#!/usr/bin/env python3
import json
import subprocess

# List of GitHub repositories in 'author/project' format
repositories = [
    "libarchive/libarchive",
    "xiph/flac",
    "libunwind/libunwind",
    "google/benchmark",
    "BLAKE3-team/BLAKE3",
    "boostorg/boost",
    "openssl/openssl",
    "libressl/portable",
    "jeremy-rifkin/cpptrace",
    "google/double-conversion",
    "fmtlib/fmt",
    "google/glog",
    "Cyan4973/xxHash",
    "lz4/lz4",
    "google/brotli",
    "facebook/zstd",
    "libfuse/libfuse",
    # "microsoft/mimalloc",
    "jemalloc/jemalloc",
    "tukaani-project/xz",
    "davea42/libdwarf-code",
    "libevent/libevent",
    "nlohmann/json",
    "HowardHinnant/date",
    "nemtrif/utfcpp",
    "ericniebler/range-v3",
    "greg7mdp/parallel-hashmap",
]


# Function to fetch the latest release information for a repository
def get_latest_release(repo):
    result = subprocess.run(
        ["gh", "api", f"repos/{repo}/releases/latest"], capture_output=True, text=True
    )

    if result.returncode == 0:
        release = json.loads(result.stdout)
        tag_name = release["tag_name"]
        release_date = release["published_at"]
        return tag_name, release_date

    print(f"Failed to fetch data for {repo}")
    return None, None


# Fetch the latest release information for each repository
for repo in repositories:
    version, release_date = get_latest_release(repo)
    if version and release_date:
        [author, project] = repo.split("/")
        date = release_date.split("T")[0]
        version = version.replace(f"{project}-", "").lstrip("v")
        project = project.replace("-", "_").upper()
        version_var = f"{project}_VERSION={version}"
        print(f"{version_var:<40} # {date}")
