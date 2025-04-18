#!/usr/bin/env python3
import argparse
import logging
import subprocess
import coloredlogs
import json
import os
import sys
import platform
import datetime
import time
import tempfile
import shutil
import glob

from packaging.version import Version

# A registry for benchmark functions.
benchmark_registry = []


def benchmark(func):
    """Decorator to register a benchmark function."""
    benchmark_registry.append(func)
    return func


def needs_version(version):
    """Decorator to specify a required version for a benchmark."""

    def decorator(func):
        func.required_version = Version(version)
        return func

    return decorator


def needs_binary(binary):
    """Decorator to specify a required binary for a benchmark."""

    def decorator(func):
        func.required_binary = binary
        return func

    return decorator


def needs_tag(tag):
    """Decorator to specify a required tag for a benchmark."""

    def decorator(func):
        func.required_tag = tag
        return func

    return decorator


def without_tag(tag):
    """Decorator to specify a tag that should not be present for a benchmark."""

    def decorator(func):
        func.excluded_tag = tag
        return func

    return decorator


def binary_size_benchmark(env, binary_name):
    binary = env.config.binary(binary_name)
    res = {
        "binary": binary_name,
        "binary_size": os.path.getsize(binary),
    }
    env.sample(res)


@benchmark
@needs_binary("mkdwarfs")
def mkdwarfs_size(env):
    binary_size_benchmark(env, "mkdwarfs")


@benchmark
@needs_binary("dwarfsck")
def dwarfsck_size(env):
    binary_size_benchmark(env, "dwarfsck")


@benchmark
@needs_binary("dwarfsextract")
def dwarfsextract_size(env):
    binary_size_benchmark(env, "dwarfsextract")


@benchmark
@needs_binary("dwarfs")
def dwarfs_size(env):
    binary_size_benchmark(env, "dwarfs")


def mkdwarfs_benchmark(env, inp, args, **kwargs):
    image = env.tmp("output.dwarfs")
    res = env.mkdwarfs(
        f"-i {env.data(inp)} -o {image} {args} --force --no-progress --log-level=error",
        **kwargs,
    )
    res["image_size"] = os.path.getsize(image)
    os.remove(image)
    env.sample(res)


@benchmark
@needs_binary("mkdwarfs")
def segmenter_perl_l7(env):
    mkdwarfs_benchmark(
        env, "perl-install-small", "-C null -N4 -l7 --metadata-compression=null"
    )


@benchmark
@needs_binary("mkdwarfs")
def segmenter_perl_l9(env):
    mkdwarfs_benchmark(
        env, "perl-install-small", "-C null -N4 -l9 --metadata-compression=null"
    )


@benchmark
@needs_binary("mkdwarfs")
def compress_perl_l7(env):
    mkdwarfs_benchmark(
        env,
        "perl-install-small",
        "-N4 -l7 -C zstd:level=12 --metadata-compression=null",
        min_runs=5,
    )


@benchmark
@needs_binary("mkdwarfs")
def compress_perl_l9(env):
    mkdwarfs_benchmark(
        env,
        "perl-install-small",
        "-N4 -l9 -C lzma:level=3 --metadata-compression=null",
        min_runs=5,
    )


@benchmark
@needs_binary("mkdwarfs")
@needs_version("0.9.0")
@without_tag("minimal")
def compress_fits(env):
    mkdwarfs_benchmark(env, "2024-02-07", "-N4 --categorize")


@benchmark
@needs_binary("mkdwarfs")
@needs_version("0.8.0")
@without_tag("minimal")
def compress_pcmaudio(env):
    mkdwarfs_benchmark(env, "pcmaudio", "-N4 --categorize")


@benchmark
@needs_binary("dwarfsextract")
def extract_perl_zstd(env):
    output = env.tmp("output")
    os.makedirs(output, exist_ok=True)
    res = env.dwarfsextract(
        f"-i {env.data('perl-install-small-v0.7.5.dwarfs')} -o {output}"
    )
    shutil.rmtree(output)
    env.sample(res)


@benchmark
@needs_binary("dwarfsextract")
@without_tag("minimal")
def extract_perl_zstd_gnutar(env):
    output = env.tmp("output.tar")
    res = env.dwarfsextract(
        f"-i {env.data('perl-install-small-v0.7.5.dwarfs')} -f gnutar -o {output}"
    )
    os.remove(output)
    env.sample(res)


@benchmark
@needs_binary("dwarfsextract")
@without_tag("minimal")
def extract_perl_zstd_gnutar_devnull(env):
    res = env.dwarfsextract(
        f"-i {env.data('perl-install-small-v0.7.5.dwarfs')} -f gnutar -o /dev/null"
    )
    env.sample(res)


@benchmark
@needs_binary("dwarfsextract")
@needs_version("0.9.0")
@without_tag("minimal")
def extract_fits(env):
    output = env.tmp("output")
    os.makedirs(output, exist_ok=True)
    res = env.dwarfsextract(f"-i {env.data('2024-02-07.dwarfs')} -o {output}")
    shutil.rmtree(output)
    env.sample(res)


@benchmark
@needs_binary("dwarfsextract")
@needs_version("0.9.0")
@without_tag("minimal")
def extract_fits_gnutar(env):
    output = env.tmp("output.tar")
    res = env.dwarfsextract(f"-i {env.data('2024-02-07.dwarfs')} -f gnutar -o {output}")
    os.remove(output)
    env.sample(res)


@benchmark
@needs_binary("dwarfsextract")
@needs_version("0.8.0")
@without_tag("minimal")
def extract_pcmaudio(env):
    output = env.tmp("output")
    os.makedirs(output, exist_ok=True)
    res = env.dwarfsextract(f"-i {env.data('pcmaudio.dwarfs')} -o {output}")
    shutil.rmtree(output)
    env.sample(res)


@benchmark
@needs_binary("dwarfsextract")
@needs_version("0.9.0")
@without_tag("minimal")
def extract_pcmaudio_gnutar(env):
    output = env.tmp("output.tar")
    res = env.dwarfsextract(f"-i {env.data('pcmaudio.dwarfs')} -f gnutar -o {output}")
    os.remove(output)
    env.sample(res)


@benchmark
@needs_binary("dwarfsck")
@needs_version("0.8.0")
def dwarfsck_no_check_perl_zstd(env):
    res = env.dwarfsck(f"{env.data('perl-install-small-v0.7.5.dwarfs')} --no-check")
    env.sample(res)


@benchmark
@needs_binary("dwarfsck")
def check_integrity_perl_zstd(env):
    res = env.dwarfsck(
        f"{env.data('perl-install-small-v0.7.5.dwarfs')} --check-integrity"
    )
    env.sample(res)


@benchmark
@needs_binary("dwarfsck")
@needs_version("0.9.2")
def checksum_files_perl_zstd_sha256(env):
    res = env.dwarfsck(
        f"{env.data('perl-install-small-v0.7.5.dwarfs')} --checksum sha256"
    )
    env.sample(res)


def make_script(filename, content):
    with open(filename, "w") as f:
        f.write(content)
    os.chmod(filename, 0o755)


def mount_and_run_test(env, image, cmd, opts=None, foreground=False, **kwargs):
    mnt = env.tmp("mnt")
    os.makedirs(mnt, exist_ok=True)
    script = env.tmp("script.sh")
    if opts is None:
        opts = ""
    cmd = cmd.format(**locals())
    bg_bash = f"""#!/bin/bash
set -e
{env.config.binary("dwarfs")} {image} {mnt} {opts}
trap 'fusermount -u {mnt}' EXIT
{cmd}
"""
    fg_bash = f"""#!/bin/bash
set -e
{env.config.binary("dwarfs")} {image} {mnt} {opts} -f &
trap 'fusermount -u {mnt}' EXIT
while [[ "$(ls -1 {mnt} | wc -l)" -eq 0 ]]; do
    sleep 0.001
done
{cmd}
fusermount -u {mnt}
trap - EXIT
wait
"""
    make_script(script, fg_bash if foreground else bg_bash)
    env.sample(env.hyperfine(script, **kwargs))


@benchmark
@needs_binary("dwarfs")
def mount_and_run_emacs_l6(env):
    mount_and_run_test(
        env, env.data(f"emacs-{platform.machine()}-l6.dwarfs"), "{mnt}/AppRun --help"
    )


@benchmark
@needs_binary("dwarfs")
@needs_version("0.12.0")
def mount_and_run_emacs_l6_mmap(env):
    mount_and_run_test(
        env,
        env.data(f"emacs-{platform.machine()}-l6.dwarfs"),
        "{mnt}/AppRun --help",
        "-oblock_allocator=mmap",
    )


@benchmark
@needs_binary("dwarfs")
def mount_and_run_emacs_l6_foreground(env):
    mount_and_run_test(
        env,
        env.data(f"emacs-{platform.machine()}-l6.dwarfs"),
        "{mnt}/AppRun --help",
        foreground=True,
    )


@benchmark
@needs_binary("dwarfs")
def mount_and_run_emacs_l9(env):
    mount_and_run_test(
        env, env.data(f"emacs-{platform.machine()}-l9.dwarfs"), "{mnt}/AppRun --help"
    )


@benchmark
@needs_binary("dwarfs")
def mount_and_run_emacs_l9_foreground(env):
    mount_and_run_test(
        env,
        env.data(f"emacs-{platform.machine()}-l9.dwarfs"),
        "{mnt}/AppRun --help",
        foreground=True,
    )


@benchmark
@needs_binary("dwarfs")
def mount_and_cat_files(env):
    mount_and_run_test(
        env,
        env.data(f"perl-install-1M-zstd.dwarfs"),
        "find {mnt}/default/perl-5.2[0-9].* -type f -print0 | xargs -0 -P16 -n64 cat | dd of=/dev/null bs=1M",
        min_runs=5,
    )


@benchmark
@needs_binary("dwarfs")
@needs_version("0.12.0")
def mount_and_cat_files_mmap(env):
    mount_and_run_test(
        env,
        env.data(f"perl-install-1M-zstd.dwarfs"),
        "find {mnt}/default/perl-5.2[0-9].* -type f -print0 | xargs -0 -P16 -n64 cat | dd of=/dev/null bs=1M",
        "-oblock_allocator=mmap",
        min_runs=5,
    )


@benchmark
@needs_binary("dwarfs")
def mount_and_cat_files_foreground(env):
    mount_and_run_test(
        env,
        env.data(f"perl-install-1M-zstd.dwarfs"),
        "find {mnt}/default/perl-5.2[0-9].* -type f -print0 | xargs -0 -P16 -n64 cat | dd of=/dev/null bs=1M",
        foreground=True,
        min_runs=5,
    )


class BenchmarkEnvironment(object):
    def __init__(self, config, data_dir, output_dir, name):
        self.config = config
        self.data_dir = data_dir
        self.output = output_dir
        self.name = name

    def tmp(self, name):
        return os.path.join(self.config.tmpdir, name)

    def data(self, name):
        return os.path.join(self.data_dir, name)

    def mkdwarfs(self, *args, **kwargs):
        return self.hyperfine(self.config.binary("mkdwarfs"), *args, **kwargs)

    def dwarfs(self, *args, **kwargs):
        return self.hyperfine(self.config.binary("dwarfs"), *args, **kwargs)

    def dwarfsck(self, *args, **kwargs):
        return self.hyperfine(self.config.binary("dwarfsck"), *args, **kwargs)

    def dwarfsextract(self, *args, **kwargs):
        return self.hyperfine(self.config.binary("dwarfsextract"), *args, **kwargs)

    def hyperfine(self, *cmd, **kwargs):
        res = self.config.hyperfine(" ".join(cmd), self.name, **kwargs)
        return res["results"][0]

    def sample(self, result):
        compiler = None
        if "gcc" in self.config.tags:
            compiler = "gcc"
        if "clang" in self.config.tags:
            compiler = "clang"
        obj = {
            "name": self.name,
            "type": self.config.config_type(),
            "is_release": self.config.is_release,
            "arch": platform.machine(),
            "compiler": compiler,
            "lto": "lto" in self.config.tags,
            "minsize": "minsize" in self.config.tags,
            "minimal": "minimal" in self.config.tags,
            "musl": "musl" in self.config.tags,
            "mimalloc": "mimalloc" in self.config.tags,
            "processor": platform.processor(),
            "cpus": self.config.cpus,
            "hostname": platform.node(),
            "config": self.config.full_config,
            "version": str(self.config.version),
            "commit": self.config.commit,
            "commit_time": self.config.commit_time.timestamp(),
            "time": datetime.datetime.now().timestamp(),
            "tags": list(self.config.tags),
        }
        obj.update(result)
        version = self.config.version
        if self.config.commit:
            version = f"{version}-{self.config.commit}"
        if self.config.full_config:
            version = f"{version}-{self.config.full_config}"
        sample_file = os.path.join(
            self.output,
            f"{self.name}-{self.config.config_type()}-{platform.machine()}-{version}-{datetime.datetime.now().strftime('%Y%m%d-%H%M%S.%f')}.json",
        )
        with open(sample_file, "w") as f:
            json.dump(obj, f, indent=4)


class Config(object):
    def __init__(self, directory, filename, prefix, suffix=None):
        self.directory = directory
        self.filename = filename

        # remove prefix and suffix from filename to get version and config
        assert filename.startswith(
            prefix
        ), f"Filename {filename} does not start with prefix {prefix}"
        assert suffix is None or filename.endswith(
            suffix
        ), f"Filename {filename} does not end with suffix {suffix}"
        cfgver = filename[len(prefix) :]
        if suffix:
            cfgver = cfgver[: -len(suffix)]

        # everything before `-Linux-` is the version, everything after `-{arch}-` is the config
        parts = cfgver.split(f"-Linux-{platform.machine()}")
        assert (
            len(parts) == 2
        ), f"Filename {filename} does not contain '-Linux-{platform.machine()}'"
        verhash = parts[0]
        if len(parts[1]) == 0:
            self.full_config = None
            self.tags = set()
        else:
            assert parts[1].startswith(
                "-"
            ), f"Config {parts[1]} does not start with '-'"
            self.full_config = parts[1].lstrip("-")
            self.tags = set(parts[1].lstrip("-").split("-"))

        # the verhash contains the version, optionally followed by the number of commits and the commit hash
        parts = verhash.split("-")
        if len(parts) == 1:
            self.version = Version(parts[0])
            self.commit = None
            self.is_release = True
        else:
            assert len(parts) == 3, f"Cannot parse version from {verhash}"
            self.version = Version(parts[0])
            assert parts[2].startswith(
                "g"
            ), f"Commit hash {parts[2]} does not start with 'g'"
            self.commit = parts[2][1:]
            self.is_release = False

    def __repr__(self):
        return f"{self.__class__.__name__}(directory={self.directory}, filename={self.filename}, config={self.full_config}), version={self.version}, commit={self.commit}, tags={self.tags})"

    def has_binary(self, binary):
        """Check if the configuration has a specific binary."""
        return binary in self.binaries

    def at_least_version(self, version):
        """Check if the configuration is at least a specific version."""
        return self.version >= version

    def set_cpus(self, cpus):
        """Set the CPUs to use for the benchmark."""
        self.cpus = cpus

    def set_tmpdir(self, tmpdir):
        """Set the temporary directory for the benchmark."""
        self.tmpdir = tmpdir

    def hyperfine(self, command, benchmark_name, **kwargs):
        """Run a command using hyperfine."""
        cmd = []
        if self.cpus:
            cmd.extend(["taskset", "--cpu-list", self.cpus])
        cmd.append("hyperfine")
        # cmd.append("--show-output")
        cmd.extend(["--warmup", kwargs.get("warmup", "2")])
        min_runs = kwargs.get("min_runs")
        if min_runs is not None:
            cmd.extend(["--min-runs", str(min_runs)])
        output = os.path.join(self.tmpdir, f"__hyperfine.json")
        cmd.extend(["--export-json", output])
        cmd.extend(["--command-name", benchmark_name])
        cmd.append(command)
        logging.debug(f"Running command: {' '.join(cmd)}")
        subprocess.run(cmd, check=True)
        # parse the JSON output and remove the JSON file
        with open(output, "r") as f:
            data = json.load(f)
        os.remove(output)
        return data

    def binary(self, name):
        """Get the path to a binary."""
        path = self.binaries.get(name)
        if path is None:
            raise ValueError(
                f"Binary {name} not found in {self.__class__.__name__}({self.filename})"
            )
        return path


class StandaloneConfig(Config):
    def __init__(self, directory, tarball):
        super().__init__(directory, tarball, "dwarfs-", ".tar.zst")

    def config_type(self):
        return "standalone"

    def prepare(self):
        # Extract the tarball into the temporary directory
        tarball_path = os.path.join(self.directory, self.filename)
        logging.info(f"Extracting {tarball_path} to {self.tmpdir}")
        subprocess.run(
            ["tar", "-xf", tarball_path, "-C", self.tmpdir, "--strip-components=1"],
            check=True,
        )
        self.binaries = {
            "dwarfs": os.path.join(self.tmpdir, "sbin", "dwarfs"),
            "mkdwarfs": os.path.join(self.tmpdir, "bin", "mkdwarfs"),
            "dwarfsck": os.path.join(self.tmpdir, "bin", "dwarfsck"),
            "dwarfsextract": os.path.join(self.tmpdir, "bin", "dwarfsextract"),
        }

        # Ensure all binaries exist
        for binary in self.binaries.values():
            assert os.path.exists(binary), f"Binary {binary} does not exist"


class UniversalConfig(Config):
    def __init__(self, directory, binary):
        super().__init__(directory, binary, "dwarfs-universal-")

    def config_type(self):
        return "universal"

    def prepare(self):
        # Copy the universal binary to the temporary directory
        binary_path = os.path.join(self.directory, self.filename)
        logging.info(f"Copying {binary_path} to {self.tmpdir}")
        shutil.copy2(binary_path, self.tmpdir)
        # Symlink the binaries to the universal binary
        self.binaries = {
            "dwarfs": os.path.join(self.tmpdir, "dwarfs"),
            "mkdwarfs": os.path.join(self.tmpdir, "mkdwarfs"),
            "dwarfsck": os.path.join(self.tmpdir, "dwarfsck"),
            "dwarfsextract": os.path.join(self.tmpdir, "dwarfsextract"),
        }
        for binary in self.binaries.values():
            os.symlink(os.path.join(self.tmpdir, self.filename), binary)


class FuseExtractConfig(Config):
    def __init__(self, directory, binary):
        super().__init__(directory, binary, "dwarfs-fuse-extract-")

    def config_type(self):
        return "fuse-extract"

    def prepare(self):
        # Copy the universal binary to the temporary directory
        binary_path = os.path.join(self.directory, self.filename)
        logging.info(f"Copying {binary_path} to {self.tmpdir}")
        shutil.copy2(binary_path, self.tmpdir)
        # Symlink the binaries to the universal binary
        self.binaries = {
            "dwarfs": os.path.join(self.tmpdir, "dwarfs"),
            "dwarfsextract": os.path.join(self.tmpdir, "dwarfsextract"),
        }
        for binary in self.binaries.values():
            os.symlink(os.path.join(self.tmpdir, self.filename), binary)


def find_configurations(input_dir):
    configs = []

    def transform_and_filter(paths):
        return [
            os.path.basename(path)
            for path in paths
            if not any(x in path for x in ["-debug", "-reldbg", "-stacktrace"])
        ]

    # Find all tarballs matching `dwarfs-*Linux*.tar.zst`
    tarballs = transform_and_filter(
        glob.glob(
            os.path.join(input_dir, f"dwarfs-*Linux-{platform.machine()}*.tar.zst")
        )
    )
    configs.extend([StandaloneConfig(input_dir, tarball) for tarball in tarballs])

    # Find all universal binaries matching `dwarfs-universal-*Linux*`
    universal = transform_and_filter(
        glob.glob(
            os.path.join(input_dir, f"dwarfs-universal-*Linux-{platform.machine()}*")
        )
    )
    configs.extend([UniversalConfig(input_dir, binary) for binary in universal])

    # Find all fuse-extract binaries matching `fuse-extract-*Linux*`
    fuse_extract = transform_and_filter(
        glob.glob(
            os.path.join(input_dir, f"dwarfs-fuse-extract-*Linux-{platform.machine()}*")
        )
    )
    configs.extend([FuseExtractConfig(input_dir, binary) for binary in fuse_extract])

    return configs


def main():
    defaults = {
        "gandalf": {
            "cpus": "0-15",
        },
        "tangerinepi5b": {
            "cpus": "4-7",
        },
        "orangepi": {
            "cpus": "4-7",
        },
    }

    parser = argparse.ArgumentParser(description="Dwarfs Benchmark Runner Script")
    parser.add_argument(
        "--input-dir",
        help="Directory containing tarballs and additional binaries.",
    )
    parser.add_argument(
        "--data-dir",
        default=os.path.join(os.path.dirname(__file__), "data"),
        help="Directory containing data files for benchmarks.",
    )
    parser.add_argument(
        "--tmp-dir",
        default=os.environ.get("XDG_RUNTIME_DIR"),
        help="Temporary directory for benchmarks. Defaults to XDG_RUNTIME_DIR.",
    )
    parser.add_argument(
        "--output-dir", help="Directory to store benchmark JSON samples."
    )
    parser.add_argument(
        "--cpus",
        help="CPUs to run benchmarks on (e.g., '0-3'). Passed to taskset if provided.",
    )
    parser.add_argument(
        "--commit-time",
        default="now",
        help="Commit time for the benchmark. Defaults to 'now'.",
    )
    parser.add_argument(
        "--tag",
        action="append",
        default=[],
        help="Additional tag in KEY=VALUE format (can be used multiple times).",
    )
    parser.add_argument(
        "--log-level",
        default="INFO",
        help="Set the logging level (e.g., DEBUG, INFO, WARNING).",
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="List all available benchmarks and exit.",
    )
    parser.add_argument(
        "--only",
        action="append",
        default=[],
        help="Run only the specified benchmarks (can be used multiple times).",
    )
    parser.add_argument(
        "--config",
        action="append",
        default=[],
        help="Run only the specified configurations (can be used multiple times).",
    )
    args = parser.parse_args()

    # Set up logging with colored output
    coloredlogs.install(
        level=args.log_level,
        fmt="%(asctime)s %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
        isatty=True,
    )

    if args.list:
        print("Available benchmarks:")
        for benchmark_func in benchmark_registry:
            print(f"    {benchmark_func.__name__}")
        sys.exit(0)

    if args.input_dir is None:
        parser.error("The --input-dir argument is required.")

    if args.output_dir is None:
        parser.error("The --output-dir argument is required.")

    commit_time = (
        datetime.datetime.now()
        if args.commit_time == "now"
        else datetime.datetime.fromtimestamp(int(args.commit_time))
    )

    nodedef = defaults.get(platform.node())
    if nodedef is not None:
        logging.info(f"Using defaults for {platform.node()}: {nodedef}")
        for key, value in nodedef.items():
            if getattr(args, key) is None:
                setattr(args, key, value)

    configs = find_configurations(args.input_dir)
    benchmarks = set(args.only)

    # additional_tags = parse_extra_tags(args.tag)
    os.makedirs(args.output_dir, exist_ok=True)

    for config in configs:
        if args.config and config.full_config not in args.config:
            logging.debug(f"Skipping {config.filename} (not selected)")
            continue

        logging.info(f"Processing: {config}")
        config.set_cpus(args.cpus)
        config.commit_time = commit_time

        with tempfile.TemporaryDirectory(dir=args.tmp_dir) as temp_root:
            logging.debug(f"Using temporary directory: {temp_root}")
            config.set_tmpdir(temp_root)
            config.prepare()

            for benchmark_func in benchmark_registry:
                if benchmarks and benchmark_func.__name__ not in benchmarks:
                    logging.debug(f"Skipping {benchmark_func.__name__} (not selected)")
                    continue

                # Check if the function has required version or binary
                if hasattr(benchmark_func, "required_version"):
                    if not config.at_least_version(benchmark_func.required_version):
                        logging.info(
                            f"Skipping {benchmark_func.__name__} for {config.filename} due to version requirement {benchmark_func.required_version}."
                        )
                        continue

                if hasattr(benchmark_func, "required_binary"):
                    if not config.has_binary(benchmark_func.required_binary):
                        logging.info(
                            f"Skipping {benchmark_func.__name__} for {config.filename} due to missing {benchmark_func.required_binary}."
                        )
                        continue

                if hasattr(benchmark_func, "required_tag"):
                    if benchmark_func.required_tag not in config.tags:
                        logging.info(
                            f"Skipping {benchmark_func.__name__} for {config.filename} due to missing tag {benchmark_func.required_tag}."
                        )
                        continue

                if hasattr(benchmark_func, "excluded_tag"):
                    if benchmark_func.excluded_tag in config.tags:
                        logging.info(
                            f"Skipping {benchmark_func.__name__} for {config.filename} due to excluded tag {benchmark_func.excluded_tag}."
                        )
                        continue

                # Call the benchmark function
                benchmark_func(
                    BenchmarkEnvironment(
                        config, args.data_dir, args.output_dir, benchmark_func.__name__
                    )
                )


if __name__ == "__main__":
    main()
