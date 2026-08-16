"""chipsalliance/sv-tests suite driver.

The upstream corpus describes the phase to exercise and the expected outcome
in each test's inline metadata. Positive simulation tests additionally emit
``:assert: <python-expression>`` records that must all evaluate true. Expected
runtime-failure tests are instead judged by an ordinary nonzero simulator exit.
This driver preserves those contracts while invoking Obelisk directly.

``--design`` is a compile-only smoke test for cores that provide an explicit
file list. It intentionally skips cores with only tool-specific manifests:
recursively compiling every source in lexical order gives misleading results.
"""

from __future__ import annotations

import ast
import hashlib
import json
import os
import re
import shlex
import subprocess
import tempfile
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

from .. import model, runner

NAME = "svtests"
SOURCE = model.GitSource(
    url="https://github.com/chipsalliance/sv-tests.git",
    rev="682f19035fc21c85b653661d8e1a6e4fe1acbf08",
)

# This pinned test states in its own source that the assertion fails on every
# tested simulator: gnt2 becomes visible one sampled edge after the later arm
# of the `and` sequence completes. Keep this distinct from metadata-driven
# should-fail tests, which require an ordinary nonzero simulator exit.
_EXPECTED_ASSERTION_FAILURES = {
    "chapter-16/16.7--sequence-and-uvm.sv",
}

_METADATA = re.compile(r"^\s*:([a-zA-Z_-]+):\s*(.+)$")
_ASSERTION = re.compile(r":assert:(.*)")
_PATTERN_STRING = re.compile(r"''(\{.*?\})'")
_MODE_ORDER = (
    "simulation",
    "simulation_without_run",
    "elaboration",
    "parsing",
    "preprocessing",
)
_FRONTEND_MODES = {"elaboration", "parsing", "preprocessing"}
_SOURCE_SUFFIXES = {".v", ".sv"}
_FILELIST_VARIABLE = re.compile(
    r"\$(?:\{([A-Za-z_][A-Za-z0-9_]*)\}|([A-Za-z_][A-Za-z0-9_]*))")
_RESULT_CACHE_SCHEMA = 1
_SUCCESS_STATUSES = {model.PASS, model.XFAIL_PASS}


def _path_identity(
        path: Path, *, digest: bool = False,
) -> dict[str, int | str]:
    """Return a cheap identity for a generated tool or build stamp."""
    try:
        stat = path.stat()
    except OSError:
        return {"path": str(path), "missing": 1}
    identity: dict[str, int | str] = {
        "path": str(path.resolve()),
        "size": stat.st_size,
        "mtime_ns": stat.st_mtime_ns,
    }
    if digest and path.is_file():
        identity["sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
    return identity


def _compiler_identity(obelisk: str) -> list[dict[str, int | str]]:
    """Identify the driver and the build generation containing its DSOs."""
    binary = Path(obelisk).resolve()
    identity = [_path_identity(binary, digest=True)]
    for parent in binary.parents:
        if (parent / "build.ninja").is_file():
            identity.append(_path_identity(parent / ".ninja_log", digest=True))
            break
    return identity


def _suite_inputs_digest(root: Path) -> str:
    """Hash every source/include input reachable through the pinned manifest."""
    files: set[Path] = set()
    directories: set[Path] = set()
    libraries = root / "conf" / "runners" / "libs.json"
    if libraries.is_file():
        files.add(libraries.resolve())
    for test in _tests(root):
        values = _metadata(test)
        sources, includes, _ = _resolve_test_inputs(root, test, values)
        files.update(Path(source).resolve() for source in sources)
        directories.update(Path(include).resolve() for include in includes)
    for directory in sorted(directories):
        if directory.is_dir():
            files.update(
                path.resolve() for path in directory.rglob("*")
                if path.is_file()
            )

    digest = hashlib.sha256()
    for path in sorted(files):
        digest.update(str(path).encode("utf-8"))
        digest.update(b"\0")
        try:
            digest.update(hashlib.sha256(path.read_bytes()).digest())
        except OSError:
            digest.update(b"<missing>")
        digest.update(b"\0")
    return digest.hexdigest()


def _cache_fingerprint(
        obelisk: str,
        root: Path,
        args,
        vpi_code: tuple[str, ...],
        vpi_mode: str | None,
) -> str:
    """Fingerprint every run-wide input that may affect a cached success."""
    git_dir = root / ".git"
    harness_files = [Path(__file__), Path(runner.__file__), Path(model.__file__)]
    payload = {
        "schema": _RESULT_CACHE_SCHEMA,
        "compiler": _compiler_identity(obelisk),
        "harness": {
            str(path.resolve()): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in harness_files
        },
        "suite_root": str(root.resolve()),
        "suite_revision": SOURCE.rev,
        "suite_head": _path_identity(git_dir / "HEAD", digest=True),
        "suite_index": _path_identity(git_dir / "index", digest=True),
        "suite_inputs": _suite_inputs_digest(root),
        "timeout": args.timeout,
        "vpi_mode": vpi_mode,
        "vpi_code": [
            _path_identity(Path(path), digest=True) for path in vpi_code
        ],
        "native_tools": {
            name: os.environ.get(name) for name in ("CC", "CXX")
        },
    }
    encoded = json.dumps(payload, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(encoded.encode("utf-8")).hexdigest()


def _test_fingerprint(test: Path) -> str:
    return hashlib.sha256(test.read_bytes()).hexdigest()


class _PassCache:
    """Incrementally persist successful tests for one exact run generation."""

    def __init__(
            self, path: Path, fingerprint: str, resume: bool = True,
            enabled: bool = True,
    ) -> None:
        self.path = path
        self.fingerprint = fingerprint
        self.enabled = enabled
        self.entries: dict[str, dict[str, str]] = {}
        self.dirty = 0
        if not enabled or not resume:
            return
        try:
            document = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError, TypeError):
            return
        if (isinstance(document, dict)
                and document.get("schema") == _RESULT_CACHE_SCHEMA
                and document.get("fingerprint") == fingerprint
                and isinstance(document.get("entries"), dict)):
            self.entries = document["entries"]

    def lookup(self, name: str, test: Path) -> model.Outcome | None:
        if not self.enabled:
            return None
        entry = self.entries.get(name)
        if not isinstance(entry, dict):
            return None
        status = entry.get("status")
        if (status not in _SUCCESS_STATUSES
                or entry.get("test") != _test_fingerprint(test)):
            return None
        return model.Outcome(status)

    def record(self, name: str, test: Path, outcome: model.Outcome) -> None:
        if not self.enabled:
            return
        if outcome.status in _SUCCESS_STATUSES:
            self.entries[name] = {
                "status": outcome.status,
                "test": _test_fingerprint(test),
            }
        else:
            self.entries.pop(name, None)
        self.dirty += 1
        if self.dirty >= 8:
            self.flush()

    def flush(self) -> None:
        if not self.dirty:
            return
        self.path.parent.mkdir(parents=True, exist_ok=True)
        document = {
            "schema": _RESULT_CACHE_SCHEMA,
            "fingerprint": self.fingerprint,
            "entries": self.entries,
        }
        temporary = self.path.with_name(
            f".{self.path.name}.{os.getpid()}.tmp")
        temporary.write_text(
            json.dumps(document, sort_keys=True, separators=(",", ":")),
            encoding="utf-8",
        )
        os.replace(temporary, self.path)
        self.dirty = 0


def _metadata(test: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in test.read_text(encoding="utf-8", errors="replace").splitlines():
        match = _METADATA.match(line)
        if match:
            values[match.group(1).lower()] = match.group(2).strip()
    return values


def _libs(root: Path) -> dict:
    path = root / "conf" / "runners" / "libs.json"
    if path.is_file():
        return json.loads(path.read_text(encoding="utf-8"))
    return {}


def _tests(root: Path) -> list[Path]:
    return sorted((root / "tests").rglob("*.sv"))


def _select_tests(root: Path, specs: list[str]) -> list[Path]:
    """Resolve CLI test spellings against the suite's tests directory."""
    if not specs:
        return _tests(root)

    tests_dir = (root / "tests").resolve()
    selected: list[Path] = []
    seen: set[Path] = set()
    for spec in specs:
        spelling = Path(spec)
        candidates = [spelling, tests_dir / spelling]
        if spelling.suffix.lower() != ".sv":
            candidates += [
                Path(str(spelling) + ".sv"),
                Path(str(tests_dir / spelling) + ".sv"),
            ]
        found = next(
            (candidate.resolve() for candidate in candidates
             if candidate.is_file()),
            None,
        )
        if found is None and len(spelling.parts) == 1:
            matches = list(tests_dir.rglob(
                spelling.name if spelling.suffix.lower() == ".sv"
                else spelling.name + ".sv"))
            if len(matches) == 1:
                found = matches[0].resolve()
            elif len(matches) > 1:
                raise SystemExit(
                    f"sv-tests test name is ambiguous: {spec}; "
                    "use a tests-relative path")
        if found is None:
            raise SystemExit(f"sv-tests test not found: {spec}")
        try:
            found.relative_to(tests_dir)
        except ValueError:
            raise SystemExit(
                f"sv-tests test is outside {tests_dir}: {spec}") from None
        if found not in seen:
            selected.append(found)
            seen.add(found)
    return selected


def _mode(values: dict[str, str]) -> str | None:
    declared = values.get("type", "parsing elaboration").split()
    return next((mode for mode in _MODE_ORDER if mode in declared), None)


def _compatible(values: dict[str, str]) -> bool:
    runners = values.get("compatible-runners", "all").split()
    return "all" in runners or "obelisk" in runners


def _library_submodules(root: Path, tests: list[Path]) -> list[str]:
    """Return top-level suite submodules needed by the selected tests."""
    tags = {
        tag
        for test in tests
        for tag in _metadata(test).get("tags", "").split()
    }
    paths: set[str] = set()
    for key, library in _libs(root).items():
        if key not in tags:
            continue
        for spelling in [*library.get("files", []),
                         *library.get("incdirs", [])]:
            parts = Path(spelling).parts
            if len(parts) >= 2:
                paths.add(str(Path("third_party", *parts[:2])))
    return sorted(paths)


def _update_submodules(root: Path, paths: list[str]) -> None:
    """Initialize explicitly requested suite inputs and report failures."""
    if not paths:
        return
    try:
        subprocess.run(
            ["git", "submodule", "update", "--init", "--recursive",
             "--depth", "1", "--", *paths],
            cwd=root,
            capture_output=True,
            text=True,
            timeout=600,
            check=True,
        )
    except subprocess.TimeoutExpired as error:
        raise SystemExit(
            "sv-tests submodule initialization exceeded 600 seconds") from error
    except subprocess.CalledProcessError as error:
        detail = (error.stderr or error.stdout).strip()
        raise SystemExit(
            f"could not initialize sv-tests submodules: {detail}") from error


def _resolve_test_inputs(
        root: Path, test: Path, values: dict[str, str],
) -> tuple[list[str], list[str], list[str]]:
    tags = values.get("tags", "").split()
    extra_sources: list[str] = []
    extra_includes: list[str] = []
    for key, library in _libs(root).items():
        if key in tags:
            extra_sources += [
                str(root / "third_party" / path)
                for path in library.get("files", [])
            ]
            extra_includes += [
                str(root / "third_party" / path)
                for path in library.get("incdirs", [])
            ]

    tests_dir = root / "tests"
    sources: list[str] = []
    default_source = str(test.relative_to(tests_dir))
    for spelling in values.get("files", default_source).split():
        path = Path(spelling)
        if not path.is_absolute():
            path = tests_dir / path
        sources.append(str(path.resolve()))

    includes: list[str] = []
    default_include = str(test.parent.relative_to(tests_dir))
    for spelling in values.get("incdirs", default_include).split():
        path = Path(spelling)
        if not path.is_absolute():
            path = tests_dir / path
        includes.append(str(path.resolve()))

    return extra_sources + sources, extra_includes + includes, (
        values.get("defines", "").split())


def _safe_assertion_value(expression: str) -> bool:
    """Evaluate the deliberately small expression language emitted by sv-tests."""
    # Assignment-pattern text begins with an apostrophe. Some upstream tests
    # place that text inside a single-quoted Python string without escaping the
    # apostrophe, producing tokens such as ``''{valid:10}'``. Recover the
    # intended string before parsing while leaving the accepted AST unchanged.
    expression = _PATTERN_STRING.sub(
        lambda match: repr("'" + match.group(1)), expression)
    tree = ast.parse(expression, mode="eval")
    allowed = (
        ast.Expression,
        ast.Constant,
        ast.Name,
        ast.Load,
        ast.Call,
        ast.BoolOp,
        ast.And,
        ast.Or,
        ast.UnaryOp,
        ast.Not,
        ast.UAdd,
        ast.USub,
        ast.BinOp,
        ast.Add,
        ast.Sub,
        ast.Mult,
        ast.Div,
        ast.FloorDiv,
        ast.Mod,
        ast.Pow,
        ast.LShift,
        ast.RShift,
        ast.BitAnd,
        ast.BitOr,
        ast.BitXor,
        ast.Invert,
        ast.Compare,
        ast.Eq,
        ast.NotEq,
        ast.Lt,
        ast.LtE,
        ast.Gt,
        ast.GtE,
        ast.In,
        ast.NotIn,
    )
    for node in ast.walk(tree):
        if not isinstance(node, allowed):
            raise ValueError(
                f"unsupported assertion expression node: "
                f"{type(node).__name__}")
        if isinstance(node, ast.Name) and node.id not in {
                "True", "False", "int", "float"}:
            raise ValueError(f"unsupported assertion name: {node.id}")
        if isinstance(node, ast.Call):
            if (not isinstance(node.func, ast.Name)
                    or node.func.id not in {"int", "float"}
                    or node.keywords):
                raise ValueError("unsupported assertion call")
    value = eval(  # noqa: S307 - AST and globals are restricted above.
        compile(tree, "<sv-tests assertion>", "eval"),
        {"__builtins__": {}, "int": int, "float": float},
        {},
    )
    return bool(value)


def _assertions_pass(output: str) -> tuple[bool, str]:
    for line in output.splitlines():
        match = _ASSERTION.search(line.strip())
        if not match:
            continue
        expression = match.group(1).strip()
        try:
            passed = _safe_assertion_value(expression)
        except (SyntaxError, TypeError, ValueError, ZeroDivisionError) as error:
            return False, f"invalid :assert: expression {expression!r}: {error}"
        if not passed:
            return False, f"failed :assert: expression: {expression}"
    return True, ""


def _test_timeout(values: dict[str, str], default: float) -> float:
    spelling = values.get("timeout")
    if spelling is None:
        return default
    try:
        return max(default, float(spelling))
    except ValueError as error:
        raise ValueError(f"invalid test timeout: {spelling!r}") from error


def _compile_threads_per_test(jobs: int, task_count: int) -> int:
    """Divide the host thread budget across concurrently compiled tests."""
    hardware_threads = runner.available_cpu_count()
    active_tests = max(1, min(jobs, task_count))
    return max(1, hardware_threads // active_tests)


def _expected_failure(
        rel: str, compiled: runner.CompileResult,
) -> tuple[str, model.Outcome] | None:
    if compiled.ok:
        return None
    if compiled.failure_kind == "compile":
        return rel, model.Outcome(model.XFAIL_PASS)
    return rel, model.Outcome(model.COMPILE_FAIL, compiled.stderr)


def judge_one(
        obelisk: str,
        root: Path,
        test: Path,
        compile_timeout: float,
        run_timeout: float,
        vpi_code: tuple[str, ...] = (),
        vpi_mode: str | None = None,
        compile_threads: int | None = None,
) -> tuple[str, model.Outcome]:
    """Compile, optionally run, and judge one sv-tests test."""
    rel = str(test.relative_to(root / "tests"))
    values = _metadata(test)
    mode = _mode(values)
    if mode is None:
        return rel, model.Outcome(model.SKIP, "no supported test type")
    if not _compatible(values):
        return rel, model.Outcome(
            model.SKIP, "test is not compatible with the Obelisk runner")

    expected_fail = (
        values.get("should_fail", "0") == "1"
        or "should_fail_because" in values
    )
    expected_assertion_failure = rel in _EXPECTED_ASSERTION_FAILURES
    sources, includes, defines = _resolve_test_inputs(root, test, values)
    missing = [
        spelling for spelling in [*sources, *includes]
        if not Path(spelling).exists()
    ]
    if missing:
        return rel, model.Outcome(
            model.SKIP,
            "missing suite input (use --fetch to initialize submodules): "
            + ", ".join(missing),
        )

    flags: list[str] = []
    for include in includes:
        flags += ["-I", include]
    for define in defines:
        flags += ["-D", define]
    is_uvm = "uvm" in values.get("tags", "").split()
    if is_uvm:
        # The pinned suite supplies the SystemVerilog UVM package but not its
        # optional C DPI implementation.  UVM_NO_DPI selects the library's
        # portable fallback, while bytecode avoids recompiling the full class
        # library through the slower native packed-lowering path for every
        # standalone sv-test.
        if not any(define.split("=", 1)[0] == "UVM_NO_DPI"
                   for define in defines):
            flags += ["-D", "UVM_NO_DPI"]
        if mode not in _FRONTEND_MODES:
            flags += ["--execution-tier=bytecode", "-fno-lto"]
    top = values.get("top_module", "")
    if top:
        flags.append("--top=" + top)
    if compile_threads is not None:
        flags.append(f"--compile-threads={compile_threads}")

    try:
        timeout = _test_timeout(values, run_timeout)
    except ValueError as error:
        return rel, model.Outcome(model.SKIP, str(error))
    frontend_timeout = max(compile_timeout, timeout)
    if is_uvm:
        # Even a tiny UVM test reparses and lowers the complete class library.
        # With one compiler thread per parallel corpus worker that can exceed
        # the ordinary 6x execution allowance without being stuck.
        frontend_timeout = max(frontend_timeout, run_timeout * 12)
    with tempfile.TemporaryDirectory(prefix="obelisk-svt-") as tmp:
        if mode == "preprocessing":
            output = str(Path(tmp) / "preprocessed.sv")
            compiled = runner.compile_preprocessor(
                obelisk,
                sources,
                output,
                flags,
                std="1800-2017",
                single_unit=True,
                timeout=frontend_timeout,
            )
        elif mode in _FRONTEND_MODES:
            output = str(Path(tmp) / "frontend.mlir")
            compiled = runner.compile_frontend(
                obelisk,
                sources,
                output,
                flags,
                std="1800-2017",
                single_unit=True,
                timeout=frontend_timeout,
            )
        else:
            native = runner.build_vpi_inputs(
                obelisk,
                list(vpi_code),
                tmp,
                cwd=str(root),
                module_name="svtests_" + re.sub(r"[^A-Za-z0-9_]", "_", rel),
            )
            if not native.ok:
                return rel, model.Outcome(model.COMPILE_FAIL, native.stderr)
            binary = Path(tmp) / "sim"
            compiled = runner.compile_design(
                obelisk,
                sources,
                str(binary),
                flags,
                std="1800-2017",
                single_unit=True,
                timeout=frontend_timeout,
                native_inputs=native.inputs,
                vpi=vpi_mode or ("full" if native.inputs else "off"),
            )

        if expected_fail:
            outcome = _expected_failure(rel, compiled)
            if outcome is not None:
                return outcome
        elif not compiled.ok:
            return rel, model.Outcome(model.COMPILE_FAIL, compiled.stderr)

        if mode in _FRONTEND_MODES or mode == "simulation_without_run":
            status = model.RUN_FAIL if expected_fail else model.PASS
            return rel, model.Outcome(status)

        # Keep files created through relative SystemVerilog paths inside this
        # test's temporary directory instead of polluting the caller's cwd.
        result = runner.execute(str(binary), timeout, cwd=tmp)
        output = result.stdout + result.stderr
        if expected_assertion_failure:
            if result.timed_out or not result.ok:
                return rel, model.Outcome(model.RUN_FAIL, output)
            markers = [
                match.group(1).strip()
                for line in output.splitlines()
                if (match := _ASSERTION.search(line))
            ]
            if not markers or any(marker != "(False)" for marker in markers):
                return rel, model.Outcome(
                    model.RUN_FAIL,
                    "documented upstream assertion failure did not emit its "
                    "exact false marker\n" + output,
                )
            return rel, model.Outcome(model.XFAIL_PASS)
        if expected_fail:
            if result.timed_out:
                return rel, model.Outcome(model.RUN_FAIL, "timeout\n" + output)
            if not result.ok:
                # A should-fail simulation may emit a failing :assert: record.
                # Re-evaluating it as a positive test would invert the suite's
                # expectation.  Still reject launch failures and signal
                # crashes: only an ordinary nonzero simulator status is an
                # expected runtime failure.
                if result.returncode is None or result.returncode <= 0:
                    return rel, model.Outcome(model.RUN_FAIL, output)
                return rel, model.Outcome(model.XFAIL_PASS)
            return rel, model.Outcome(model.RUN_FAIL, output)

        if result.timed_out:
            return rel, model.Outcome(model.RUN_FAIL, "timeout\n" + output)
        if not result.ok:
            return rel, model.Outcome(model.RUN_FAIL, output)
        assertions_ok, detail = _assertions_pass(output)
        if not assertions_ok:
            return rel, model.Outcome(model.RUN_FAIL, detail + "\n" + output)
        return rel, model.Outcome(model.PASS)


def _expand_filelist_token(
        spelling: str, core_dir: Path, filelist_dir: Path,
) -> Path:
    def replace(match: re.Match[str]) -> str:
        name = match.group(1) or match.group(2)
        if name in os.environ:
            return os.environ[name]
        if name.endswith("_ROOT"):
            return str(core_dir)
        raise ValueError(f"unresolved file-list variable ${name}")

    expanded = _FILELIST_VARIABLE.sub(replace, spelling)
    path = Path(expanded)
    if not path.is_absolute():
        path = filelist_dir / path
    return path.resolve()


def _read_filelist(
        path: Path,
        core_dir: Path,
        seen: set[Path] | None = None,
) -> tuple[list[str], list[str], list[str]]:
    """Read a Verilog ``.f`` file while preserving source order."""
    path = path.resolve()
    seen = set() if seen is None else seen
    if path in seen:
        return [], [], []
    seen.add(path)

    sources: list[str] = []
    includes: list[str] = []
    defines: list[str] = []
    words: list[str] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        words.extend(shlex.split(line, comments=True, posix=True))

    index = 0
    while index < len(words):
        word = words[index]
        index += 1
        if word in {"-f", "-F"}:
            if index == len(words):
                raise ValueError(f"{path}: {word} requires a path")
            nested = _expand_filelist_token(
                words[index], core_dir, path.parent)
            index += 1
            nested_sources, nested_includes, nested_defines = _read_filelist(
                nested, core_dir, seen)
            sources += nested_sources
            includes += nested_includes
            defines += nested_defines
        elif word.startswith("+incdir+"):
            for spelling in word[len("+incdir+"):].split("+"):
                if spelling:
                    includes.append(str(_expand_filelist_token(
                        spelling, core_dir, path.parent)))
        elif word == "-I":
            if index == len(words):
                raise ValueError(f"{path}: -I requires a path")
            includes.append(str(_expand_filelist_token(
                words[index], core_dir, path.parent)))
            index += 1
        elif word.startswith("-I"):
            includes.append(str(_expand_filelist_token(
                word[2:], core_dir, path.parent)))
        elif word.startswith("+define+"):
            defines += [
                define for define in word[len("+define+"):].split("+")
                if define
            ]
        elif word == "-D":
            if index == len(words):
                raise ValueError(f"{path}: -D requires a value")
            defines.append(words[index])
            index += 1
        elif word.startswith("-D"):
            defines.append(word[2:])
        elif Path(word).suffix.lower() in _SOURCE_SUFFIXES:
            sources.append(str(_expand_filelist_token(
                word, core_dir, path.parent)))
        else:
            raise ValueError(f"{path}: unsupported file-list option {word!r}")

    return (
        list(dict.fromkeys(sources)),
        list(dict.fromkeys(includes)),
        list(dict.fromkeys(defines)),
    )


def _design_sources(core_dir: Path) -> tuple[list[str], list[str], list[str]]:
    """Load a core's explicit file list; never guess source order."""
    core_dir = core_dir.resolve()
    preferred = core_dir / "compile.f"
    filelists = [preferred] if preferred.is_file() else sorted(
        core_dir.glob("*.f"))
    if len(filelists) != 1:
        raise ValueError(
            "no unambiguous root .f manifest; FuseSoC/Bender-only cores "
            "require an exported file list")
    return _read_filelist(filelists[0], core_dir)


def _compile_design(
        obelisk: str,
        root: Path,
        design: str,
        compile_timeout: float,
        compile_threads: int,
) -> tuple[str, model.Outcome]:
    core_dir = root / "third_party" / "cores" / design
    if not core_dir.is_dir():
        return design, model.Outcome(
            model.SKIP, f"core not found: {core_dir}")
    try:
        sources, incdirs, defines = _design_sources(core_dir)
    except (OSError, ValueError) as error:
        return design, model.Outcome(model.SKIP, str(error))
    missing = [source for source in sources if not Path(source).is_file()]
    if missing:
        return design, model.Outcome(
            model.SKIP, "manifest source is missing: " + ", ".join(missing))

    flags: list[str] = []
    for include in incdirs:
        flags += ["-I", include]
    for define in defines:
        flags += ["-D", define]
    flags.append(f"--compile-threads={compile_threads}")
    with tempfile.TemporaryDirectory(prefix="obelisk-dgn-") as tmp:
        compiled = runner.compile_frontend(
            obelisk,
            sources,
            str(Path(tmp) / "design.mlir"),
            flags,
            std="1800-2017",
            single_unit=True,
            timeout=compile_timeout,
        )
    if compiled.ok:
        return design, model.Outcome(model.PASS)
    return design, model.Outcome(model.COMPILE_FAIL, compiled.stderr)


def run(root: Path, args) -> dict[str, model.Outcome]:
    """Select, compile, run, and judge the sv-tests corpus."""
    if args.jobs < 1:
        raise SystemExit("--jobs must be at least 1")
    obelisk = args.obelisk_binary
    compile_timeout = args.timeout * 6
    design = getattr(args, "design", None)

    if design:
        if design == "all":
            cores_dir = root / "third_party" / "cores"
            designs = sorted(
                directory.name
                for directory in cores_dir.iterdir()
                if directory.is_dir() and not directory.name.startswith(".")
            )
        else:
            designs = [design]
        if getattr(args, "fetch", False):
            _update_submodules(
                root,
                [str(Path("third_party", "cores", name))
                 for name in designs],
            )
        print(f"Compiling {len(designs)} sv-tests designs with "
              f"-j{args.jobs} ...")
        compile_threads = _compile_threads_per_test(args.jobs, len(designs))
        print(f"Using {compile_threads} Obelisk compile thread(s) per design")
        tasks = [
            (obelisk, root, name, compile_timeout, compile_threads)
            for name in designs
        ]
        if not tasks:
            return {}
        if args.jobs == 1:
            results = [_compile_design(*task) for task in tasks]
        else:
            with ProcessPoolExecutor(max_workers=args.jobs) as pool:
                results = list(pool.map(_compile_design, *zip(*tasks)))
        return dict(results)

    tests = _select_tests(root, args.tests)
    if getattr(args, "fetch", False):
        _update_submodules(root, _library_submodules(root, tests))
    vpi_code = tuple(
        str(Path(path).resolve()) for path in getattr(args, "vpi_code", []))
    vpi_mode = getattr(args, "vpi", None)
    default_cache = Path(__file__).resolve().parents[2] / "cache" / "results"
    cache_path = Path(getattr(args, "result_cache", None)
                      or default_cache / "svtests.json")
    cache = _PassCache(
        cache_path,
        _cache_fingerprint(obelisk, root, args, vpi_code, vpi_mode),
        resume=not getattr(args, "no_resume", False),
        # Compiled VPI sources can include arbitrary transitive C/C++ headers.
        # Without a depfile, conservatively rerun rather than cache them.
        enabled=not vpi_code,
    )

    outcomes: dict[str, model.Outcome] = {}
    pending: list[Path] = []
    for test in tests:
        name = str(test.relative_to(root / "tests"))
        cached = cache.lookup(name, test)
        if cached is None:
            pending.append(test)
        else:
            outcomes[name] = cached

    print(f"Running {len(tests)} sv-tests tests with -j{args.jobs} ...")
    if outcomes:
        print(f"Reusing {len(outcomes)} unchanged successful result(s); "
              f"running {len(pending)} test(s)")
    compile_threads = _compile_threads_per_test(args.jobs, len(pending))
    if pending:
        print(f"Using {compile_threads} Obelisk compile thread(s) per test")

    tasks = [
        (obelisk, root, test, compile_timeout, args.timeout,
         vpi_code, vpi_mode, compile_threads)
        for test in pending
    ]
    try:
        if args.jobs == 1:
            for test, task in zip(pending, tasks):
                name, outcome = judge_one(*task)
                outcomes[name] = outcome
                cache.record(name, test, outcome)
        else:
            # Keep only a small bounded queue beyond the active workers. This
            # avoids handing the entire corpus to multiprocessing at once and
            # makes cancellation wait for active tests, not every queued test.
            pool = ProcessPoolExecutor(max_workers=args.jobs)
            work = iter(zip(pending, tasks))
            futures = {}

            def submit_next() -> bool:
                try:
                    test, task = next(work)
                except StopIteration:
                    return False
                futures[pool.submit(judge_one, *task)] = test
                return True

            try:
                for _ in range(min(len(tasks), args.jobs * 2)):
                    submit_next()
                while futures:
                    future = next(as_completed(futures))
                    test = futures.pop(future)
                    name, outcome = future.result()
                    outcomes[name] = outcome
                    cache.record(name, test, outcome)
                    submit_next()
            except BaseException:
                for future in futures:
                    future.cancel()
                pool.shutdown(wait=True, cancel_futures=True)
                # Active workers can finish while shutdown waits. Preserve
                # those successful results too before propagating the signal.
                for future, test in futures.items():
                    if future.cancelled() or not future.done():
                        continue
                    try:
                        name, outcome = future.result()
                    except BaseException:
                        continue
                    outcomes[name] = outcome
                    cache.record(name, test, outcome)
                raise
            else:
                pool.shutdown(wait=True)
    finally:
        cache.flush()
    return dict(sorted(outcomes.items()))
