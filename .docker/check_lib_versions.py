#!/usr/bin/env python3
import requests

# List of GitHub repositories in 'author/project' format
repositories = [
    "libarchive/libarchive",
    "xiph/flac",
    "libunwind/libunwind",
    "google/benchmark",
    "openssl/openssl",
    "jeremy-rifkin/cpptrace",
    "google/double-conversion",
    "fmtlib/fmt",
    "google/glog",
    "Cyan4973/xxHash",
    "lz4/lz4",
    "google/brotli",
    "facebook/zstd",
    "libfuse/libfuse",
    "microsoft/mimalloc",
    "boostorg/boost",
]

# Function to fetch the latest release information for a repository
def get_latest_release(repo):
    url = f"https://api.github.com/repos/{repo}/releases/latest"
    response = requests.get(url)

    if response.status_code == 200:
        release = response.json()
        tag_name = release['tag_name']
        release_date = release['published_at']
        return tag_name, release_date
    else:
        print(f"Failed to fetch data for {repo}")
        return None, None

# Fetch the latest release information for each repository
for repo in repositories:
    version, release_date = get_latest_release(repo)
    if version and release_date:
        [author, project] = repo.split('/')
        date = release_date.split('T')[0]
        version = version.replace(f"{project}-", "").lstrip('v')
        project = project.replace('-', '_').upper()
        version_var = f"{project}_VERSION={version}"
        print(f"{version_var:<40} # {date}")
