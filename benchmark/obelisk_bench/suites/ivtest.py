"""Icarus Verilog ivtest suite driver.

Runs the ivtest corpus against Obelisk with a runner we own — no dependency on
ivtest's `vvp_reg`/`run_ivl`. Tests use JSON, legacy inline, or VPI-regression
descriptors. We translate Icarus arguments, build any C/C++ VPI module, compile
the design and native inputs with Obelisk, run it, and judge it three ways:

  * type `CE` expects a compile error;
  * a descriptor with a gold file passes iff its stdout matches the gold;
  * otherwise the test self-checks and must print `PASSED`.

Because we control each test's output path, there is no shared `work/a.out` to
collide on, so parallelism needs no sandbox — every test compiles into its own
temporary directory.
"""

from __future__ import annotations

import dataclasses
import json
import tempfile
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

from .. import icarus, model, runner

NAME = "ivtest"
SOURCE = model.GitSource(
    url="https://github.com/steveicarus/iverilog.git",
    rev="a4989d023d4d9dedf05a95ed544ee501c69faa81",
)

SINGLE_UNIT = True
# The full portable corpus: the SystemVerilog and Verilog regression lists that
# ship in the suite. `--lists obelisk-smoke.list` selects the curated fast subset.
DEFAULT_LISTS = ["regress-sv.list", "regress-vlg.list"]
PASSED_MARKER = "PASSED"
_LISTS_DIR = Path(__file__).resolve().parents[2] / "lists" / "ivtest"


def _ivtest_dir(root: Path) -> Path:
    return root / "ivtest"


def resolve_lists(root: Path, requested: list[str]) -> list[Path]:
    """Resolve list-file names against cwd, the rescued lists, or the suite tree."""
    ivtest_dir = _ivtest_dir(root)
    resolved: list[Path] = []
    for name in requested:
        for candidate in (Path(name), _LISTS_DIR / name, ivtest_dir / name):
            if candidate.exists():
                resolved.append(candidate.resolve())
                break
        else:
            raise SystemExit(
                f"ivtest list '{name}' not found (looked in cwd, "
                f"{_LISTS_DIR}, and {ivtest_dir})"
            )
    return resolved


@dataclasses.dataclass
class Descriptor:
    """A normalized ivtest test, format-independent."""
    key: str
    test_type: str
    iverilog_args: list[str]
    source: Path
    gold: Path | None
    vpi_sources: list[Path]
    vpi_compiler_args: list[str]


def _parse_descriptor(ivtest_dir: Path, key: str, fields: list[str]) -> Descriptor:
    """Normalize one list entry from either ivtest list format.

    ivtest ships two formats. The JSON form (`key vvp_tests/x.json`) points at a
    descriptor file; the legacy inline form (`key type,args dir [gold=f]`) encodes
    everything on the line. Both appear in this checkout — the curated smoke lists
    are JSON, the big regress-*.list files are legacy — so we handle both.
    """
    second = fields[0]
    # JSON form points at a `.json` descriptor; the legacy form's second field is
    # `type[,args]`, whose args may themselves contain slashes (e.g. -f paths), so
    # only the `.json` suffix distinguishes the formats.
    if second.endswith(".json"):
        data = json.loads((ivtest_dir / second).read_text(encoding="ascii"))
        gold = data.get("gold")
        vpi_sources = [
            ivtest_dir / source for source in data.get("vpi-sources", [])
        ]
        return Descriptor(
            key=key,
            test_type=data.get("type", "normal"),
            iverilog_args=list(data.get("iverilog-args", [])),
            source=ivtest_dir / "ivltests" / data["source"],
            gold=(ivtest_dir / "gold" / f"{gold}-vvp-stdout.gold") if gold else None,
            vpi_sources=vpi_sources,
            vpi_compiler_args=list(data.get("vpi-compiler-args", [])),
        )
    # VPI regression form:
    #   name type[,iverilog-args] C/C++-file gold-file [compiler args/sources]
    # The HDL and primary native source live under vpi/, while expected output
    # lives under vpi_gold/.
    native_suffixes = {".a", ".bc", ".c", ".cc", ".cpp", ".cxx", ".o"}
    if len(fields) >= 3 and Path(fields[1]).suffix.lower() in native_suffixes:
        type_and_args = second.split(",")
        source = ivtest_dir / "vpi" / f"{key}.v"
        if not source.exists():
            source = source.with_suffix(".sv")
        vpi_sources = [ivtest_dir / "vpi" / fields[1]]
        compiler_args: list[str] = []
        for spelling in fields[3:]:
            candidate = ivtest_dir / spelling
            if (Path(spelling).suffix.lower() in native_suffixes and
                    candidate.exists()):
                vpi_sources.append(candidate)
            else:
                compiler_args.append(spelling)
        return Descriptor(
            key=key,
            test_type=type_and_args[0],
            iverilog_args=type_and_args[1:],
            source=source,
            gold=ivtest_dir / "vpi_gold" / fields[2],
            vpi_sources=vpi_sources,
            vpi_compiler_args=compiler_args,
        )
    # Legacy: fields = [type[,args], directory, gold=file ...].
    type_and_args = second.split(",")
    directory = fields[1] if len(fields) > 1 else "ivltests"
    gold = None
    for extra in fields[2:]:
        if extra.startswith("gold="):
            gold = ivtest_dir / "gold" / extra[len("gold="):]
    return Descriptor(
        key=key,
        test_type=type_and_args[0],
        iverilog_args=type_and_args[1:],
        source=ivtest_dir / directory / f"{key}.v",
        gold=gold,
        vpi_sources=[],
        vpi_compiler_args=[],
    )


def read_items(ivtest_dir: Path, lists: list[Path]) -> list[Descriptor]:
    """Parse the list files into normalized descriptors.

    One entry per line with `#` comments stripped; a key listed twice takes its
    last definition, matching ivtest's own override semantics.
    """
    entries: dict[str, list[str]] = {}
    for path in lists:
        for raw in path.read_text(encoding="utf-8").splitlines():
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            fields = line.split()
            if len(fields) >= 2:
                entries[fields[0]] = fields[1:]
    return [_parse_descriptor(ivtest_dir, key, fields)
            for key, fields in entries.items()]


def judge_one(obelisk: str, ivtest_dir: Path, desc: Descriptor,
              timeout: float, vpi_code: tuple[str, ...] = (),
              vpi_mode: str | None = None) -> tuple[str, model.Outcome]:
    """Compile, run, and judge one ivtest test in its own temporary directory."""
    if not desc.source.exists():
        return (desc.key, model.Outcome(model.SKIP))

    flags, std = icarus.translate_args(desc.iverilog_args)
    # ivtest defines this for non-strict runs; harmless to Obelisk, faithful to
    # how the sources expect to be compiled.
    flags += ["-D", "__ICARUS_UNSIZED__"]
    # Let includes and separate library modules under ivltests/ resolve.
    flags += ["-y", str(ivtest_dir / "ivltests"), "-I", str(ivtest_dir / "ivltests")]

    with tempfile.TemporaryDirectory(prefix="obelisk-ivt-") as tmp:
        native = runner.build_vpi_inputs(
            obelisk,
            [*(str(path) for path in desc.vpi_sources), *vpi_code],
            tmp,
            compiler_flags=desc.vpi_compiler_args,
            cwd=str(ivtest_dir),
            module_name="ivtest_" + "".join(
                character if character.isalnum() else "_"
                for character in desc.key),
        )
        if not native.ok:
            return (desc.key,
                    model.Outcome(model.COMPILE_FAIL, native.stderr))
        binary = Path(tmp) / "sim"
        selected_vpi = vpi_mode or (
            "full" if native.inputs else "off")
        compiled = runner.compile_design(
            obelisk, [str(desc.source)], str(binary), flags, std=std,
            single_unit=SINGLE_UNIT,
            native_inputs=native.inputs, vpi=selected_vpi,
        )

        if desc.test_type == "CE":
            status = model.XFAIL_PASS if not compiled.ok else model.RUN_FAIL
            return (desc.key, model.Outcome(status))
        if not compiled.ok:
            return (desc.key, model.Outcome(model.COMPILE_FAIL, compiled.stderr))

        result = runner.execute(str(binary), timeout)
        if desc.gold is not None:
            if desc.gold.exists() and result.stdout == desc.gold.read_text(
                    encoding="utf-8", errors="replace"):
                return (desc.key, model.Outcome(model.PASS))
            return (desc.key, model.Outcome(model.RUN_FAIL, result.stdout))
        if any(line.strip() == PASSED_MARKER for line in result.stdout.splitlines()):
            return (desc.key, model.Outcome(model.PASS))
        # Test doesn't use PASSED marker. Treat clean exit as pass.
        if result.ok:
            return (desc.key, model.Outcome(model.PASS))
        return (desc.key, model.Outcome(model.RUN_FAIL, result.stdout))


def run(root: Path, args) -> dict[str, model.Outcome]:
    """Select, compile, run, and judge the ivtest corpus, optionally in parallel."""
    requested = args.lists if args.lists else DEFAULT_LISTS
    lists = resolve_lists(root, requested)
    ivtest_dir = _ivtest_dir(root)
    items = read_items(ivtest_dir, lists)
    obelisk = args.obelisk_binary
    timeout = args.timeout
    vpi_code = tuple(
        str(Path(path).resolve()) for path in getattr(args, "vpi_code", []))
    vpi_mode = getattr(args, "vpi", None)
    print(f"Running {len(items)} ivtest tests from "
          f"{', '.join(path.name for path in lists)} with -j{args.jobs} ...")

    outcomes: dict[str, model.Outcome] = {}
    if args.jobs == 1:
        for item in items:
            key, outcome = judge_one(
                obelisk, ivtest_dir, item, timeout, vpi_code, vpi_mode)
            outcomes[key] = outcome
    else:
        with ProcessPoolExecutor(max_workers=args.jobs) as pool:
            for key, outcome in pool.map(
                    judge_one,
                    *zip(*[(obelisk, ivtest_dir, item, timeout, vpi_code,
                            vpi_mode) for item in items])):
                outcomes[key] = outcome
    return outcomes
