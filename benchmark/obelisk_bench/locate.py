"""Locating the Obelisk binary and the external suite checkouts.

The benchmark harness needs three things it does not own: the built Obelisk
driver, and a checkout of each upstream test suite. This module resolves all of
them and, on request, fetches a suite at its pinned revision. Only the test
suites are needed — neither Verilator nor Icarus is ever built.
"""

from __future__ import annotations

import os
import subprocess
from pathlib import Path

# benchmark/obelisk_bench/locate.py -> benchmark/ -> repo root.
BENCHMARK_DIR = Path(__file__).resolve().parents[1]
REPO_ROOT = BENCHMARK_DIR.parent
CACHE_DIR = BENCHMARK_DIR / "cache"


def resolve_compiler(cli_path: str | None) -> Path:
    """Resolve the Obelisk driver binary.

    Order: explicit `--obelisk` > OBELISK_BENCH_COMPILER > repo-relative build
    output. The resolved path is exported back into the environment so the PATH
    shims (which cannot take a flag) see the same binary.
    """
    if cli_path:
        binary = Path(cli_path).expanduser().resolve()
    elif os.environ.get("OBELISK_BENCH_COMPILER"):
        binary = Path(os.environ["OBELISK_BENCH_COMPILER"]).expanduser().resolve()
    else:
        binary = REPO_ROOT / "build" / "tools" / "driver" / "obelisk"
    if not binary.exists():
        raise SystemExit(
            f"Obelisk binary not found at {binary}. Build it "
            f"(ninja -C build obelisk) or pass --obelisk PATH."
        )
    os.environ["OBELISK_BENCH_COMPILER"] = str(binary)
    return binary


def resolve_suite_root(suite_name: str, cli_root: str | None, fetch: bool,
                       source) -> Path:
    """Resolve a suite's checkout directory.

    Order: explicit `--suite-root` > OBELISK_BENCH_<SUITE>_ROOT > a previously
    fetched cache > (if --fetch) a fresh fetch. Raises otherwise, telling the
    user how to supply it.
    """
    if cli_root:
        root = Path(cli_root).expanduser().resolve()
        if not root.exists():
            raise SystemExit(f"--suite-root {root} does not exist")
        return root

    env_var = f"OBELISK_BENCH_{suite_name.upper()}_ROOT"
    if os.environ.get(env_var):
        root = Path(os.environ[env_var]).expanduser().resolve()
        if not root.exists():
            raise SystemExit(f"{env_var}={root} does not exist")
        return root

    cached = CACHE_DIR / suite_name
    if fetch:
        return fetch_suite(suite_name, cached, source)
    if (cached / ".git").exists():
        return cached
    raise SystemExit(
        f"No checkout for suite '{suite_name}'. Pass --fetch to clone "
        f"{source.url} @ {source.rev[:12]}, or --suite-root PATH for a local copy."
    )


def fetch_suite(suite_name: str, dest: Path, source) -> Path:
    """Shallow-fetch a suite at its pinned revision into `dest`.

    A full history is unnecessary — one commit is all the harness reads — so this
    does an init + single-rev fetch rather than a clone, which is markedly faster
    on repositories the size of Verilator.
    """
    dest.mkdir(parents=True, exist_ok=True)
    if not (dest / ".git").exists():
        subprocess.run(["git", "init", "--quiet"], cwd=dest, check=True)
        subprocess.run(["git", "remote", "add", "origin", source.url],
                       cwd=dest, check=True)
    print(f"Fetching {suite_name} @ {source.rev[:12]} from {source.url} ...")
    subprocess.run(["git", "fetch", "--quiet", "--depth", "1", "origin", source.rev],
                   cwd=dest, check=True)
    subprocess.run(["git", "checkout", "--quiet", source.rev], cwd=dest, check=True)
    return dest


def suite_revision(root: Path) -> str:
    """Return the short git revision of a suite checkout, or "unknown"."""
    try:
        result = subprocess.run(
            ["git", "-C", str(root), "rev-parse", "--short", "HEAD"],
            capture_output=True, text=True, check=True,
        )
        return result.stdout.strip()
    except (subprocess.CalledProcessError, OSError):
        return "unknown"


def obelisk_revision() -> str:
    """Return the short git revision of the Obelisk repo, or "unknown"."""
    try:
        result = subprocess.run(
            ["git", "-C", str(REPO_ROOT), "rev-parse", "--short", "HEAD"],
            capture_output=True, text=True, check=True,
        )
        return result.stdout.strip()
    except (subprocess.CalledProcessError, OSError):
        return "unknown"
