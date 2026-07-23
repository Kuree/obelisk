import hashlib
import os
import pathlib
import shutil
import subprocess
import sys


PACKAGES = (
    (
        "libc6_2.28-10+deb10u4_amd64.deb",
        "80b59743f7b47f0644d211d56918851404e66ab7fbba17e60e90664c12fc5822",
    ),
    (
        "libc6-dev_2.28-10+deb10u4_amd64.deb",
        "d759a8102b932dc51e3a25b8cc3b91f3718adda774b0c42b431117662b4750cc",
    ),
    (
        "linux-libc-dev_4.19.316-1_amd64.deb",
        "fde95d52b753e7b9b372d22b5c7154b31bea3b9d63239ef0c40655da94c141a3",
    ),
)


def provision(cmake, script, stamp, sysroot, cache, urls, preextracted=None):
    command = [
        cmake,
        f"-DSTAMP={stamp}",
        f"-DSYSROOT={sysroot}",
        "-DTARGET_TRIPLE=x86_64-unknown-linux-gnu",
        "-DLAYOUT_VERSION=1",
        f"-DPACKAGE_CACHE={cache}",
    ]
    if preextracted is not None:
        command.append(f"-DPREEXTRACTED_SYSROOT={preextracted}")
    for index, ((name, digest), url) in enumerate(zip(PACKAGES, urls)):
        command.extend(
            (
                f"-DPACKAGE{index}_NAME={name}",
                f"-DPACKAGE{index}_URL={url}",
                f"-DPACKAGE{index}_SHA256={digest}",
            )
        )
    command.extend(("-P", str(script)))
    return subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


def fingerprint(path):
    status = path.stat()
    return status.st_ino, status.st_size, status.st_mtime_ns


cmake = sys.argv[1]
source_cache = pathlib.Path(sys.argv[2]).resolve()
configured_sysroot = pathlib.Path(sys.argv[3]).resolve()
script = pathlib.Path(sys.argv[4]).resolve()
scratch = pathlib.Path(sys.argv[5]).resolve()
source_root = pathlib.Path(sys.argv[6]).resolve()
llvm_dist = pathlib.Path(sys.argv[7]).resolve()
shutil.rmtree(scratch, ignore_errors=True)
scratch.mkdir(parents=True)

sources = [source_cache / name for name, _ in PACKAGES]
for source, (_, expected) in zip(sources, PACKAGES):
    actual = hashlib.sha256(source.read_bytes()).hexdigest()
    if actual != expected:
        raise SystemExit(f"configured package cache has wrong hash: {source}")

# A fresh file:// provision exercises verified temporary downloads, package
# extraction, atomic sysroot assembly, and the final completion stamp.
cache = scratch / "cache"
sysroot = scratch / "sysroot"
stamp = scratch / ".complete"
urls = [source.as_uri() for source in sources]
first = provision(cmake, script, stamp, sysroot, cache, urls)
if first.returncode:
    sys.stdout.buffer.write(first.stdout)
    raise SystemExit("fresh offline provisioning failed")
if not stamp.is_file() or not (sysroot / "usr/include/stdio.h").is_file():
    raise SystemExit("successful provisioning did not publish a complete sysroot")

# Once the cache and extracted sysroot are complete, unusable URLs cannot
# trigger network work and the assembled tree is not replaced.
before = fingerprint(sysroot)
missing_urls = ["file:///obelisk-intentionally-missing"] * len(PACKAGES)
second = provision(cmake, script, stamp, sysroot, cache, missing_urls)
if second.returncode:
    sys.stdout.buffer.write(second.stdout)
    raise SystemExit("cached provisioning failed")
if fingerprint(sysroot) != before:
    raise SystemExit("cached provisioning unexpectedly restaged the sysroot")

# A caller-provided sysroot bypasses package access but is still validated.
pre_stamp = scratch / "preextracted.complete"
pre = provision(
    cmake,
    script,
    pre_stamp,
    scratch / "unused-sysroot",
    scratch / "unused-cache",
    missing_urls,
    configured_sysroot,
)
if pre.returncode or not pre_stamp.is_file():
    sys.stdout.buffer.write(pre.stdout)
    raise SystemExit("pre-extracted sysroot mode failed")

# A corrupt download is removed and can never produce the success stamp.
corrupt = scratch / "corrupt.deb"
corrupt.write_bytes(b"not a Debian package\n")
bad_root = scratch / "corrupt-result"
bad = provision(
    cmake,
    script,
    bad_root / ".complete",
    bad_root / "sysroot",
    bad_root / "cache",
    [corrupt.as_uri()] * len(PACKAGES),
)
if bad.returncode == 0 or (bad_root / ".complete").exists():
    raise SystemExit("corrupt provisioning published a completion stamp")
if list((bad_root / "cache").glob("*.tmp-*")):
    raise SystemExit("corrupt provisioning left a temporary download")


def run_checked(command, message):
    result = subprocess.run(
        [str(item) for item in command],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if result.returncode:
        sys.stdout.buffer.write(result.stdout)
        raise SystemExit(message)
    return result


def configure_graph(source, build, runtime_source=None, preextracted=None,
                    layout=1):
    command = [
        cmake,
        "-S",
        source,
        "-B",
        build,
        "-G",
        "Ninja",
        f"-DOBELISK_TARGET_SYSROOT_LAYOUT_VERSION={layout}",
        f"-DOBELISK_TARGET_PACKAGE_CACHE={source_cache}",
        f"-DOBELISK_TARGET_LIBC6_URL={sources[0].as_uri()}",
        f"-DOBELISK_TARGET_LIBC6_DEV_URL={sources[1].as_uri()}",
        f"-DOBELISK_TARGET_LINUX_LIBC_DEV_URL={sources[2].as_uri()}",
    ]
    if runtime_source is not None:
        command.append(f"-DOBELISK_TARGET_RUNTIME_SOURCE_DIR={runtime_source}")
    if preextracted is not None:
        command.append(f"-DOBELISK_TARGET_SYSROOT_DIR={preextracted}")
    run_checked(command, "isolated CMake graph configuration failed")


def build_graph(build, target, expect_success=True):
    result = subprocess.run(
        [cmake, "--build", str(build), "--target", target],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if expect_success and result.returncode:
        sys.stdout.buffer.write(result.stdout)
        raise SystemExit(f"isolated CMake graph target failed: {target}")
    if not expect_success and result.returncode == 0:
        raise SystemExit(f"isolated CMake graph unexpectedly succeeded: {target}")
    return result


# Exercise the production CMake graph, not only its provisioning script. The
# miniature project builds the target runtime and staged support without
# configuring the compiler or its frontend dependencies.
graph_source = scratch / "graph-source"
graph_build = scratch / "graph-build"
graph_runtime = scratch / "graph-runtime"
graph_source.mkdir()
shutil.copytree(source_root / "runtime", graph_runtime)
(graph_source / "CMakeLists.txt").write_text(
    "\n".join(
        (
            "cmake_minimum_required(VERSION 3.20)",
            "project(ObeliskNativeGraphTest LANGUAGES NONE)",
            f'set(OBELISK_SOURCE_DIR [[{source_root}]])',
            f'set(OBELISK_LLVM_DIST_DIR [[{llvm_dist}]])',
            'set(OBELISK_LLVM_VERSION "22.1.6")',
            f'include([[{source_root / "cmake/TargetNativeSupport.cmake"}]])',
            "",
        )
    )
)
configure_graph(graph_source, graph_build, graph_runtime)
build_graph(graph_build, "obelisk_native_support")
graph_sysroot_stamps = sorted((graph_build / "target-sysroots").glob("*/.complete"))
support_stamps = sorted(graph_build.glob("native-support-*.complete"))
runtime_archive = graph_build / "target-runtime/libobelisk_rt.a"
runtime_lto_archive = graph_build / "target-runtime/libobelisk_rt_lto.a"
support = graph_build / "lib/obelisk/targets/x86_64-unknown-linux-gnu"
if len(graph_sysroot_stamps) != 1 or len(support_stamps) != 1:
    raise SystemExit("first graph build did not create one content-addressed tree")
first_sysroot = fingerprint(graph_sysroot_stamps[0])
first_runtime = fingerprint(runtime_archive)
first_runtime_lto = fingerprint(runtime_lto_archive)
first_support = fingerprint(support_stamps[0])

native_members = [
    "ABI.o",
    "Bytecode.o",
    "DesignBytecode.o",
    "DesignDatabase.o",
    "DPI.o",
    "FileIO.o",
    "Format.o",
    "Process.o",
    "Runtime.o",
]
lto_members = [
    pathlib.Path(member).with_suffix(".bc").name for member in native_members
]
archive_inspection = scratch / "archive-inspection"
archive_inspection.mkdir()


def inspect_archive(archive, expected_members, bitcode):
    members = run_checked(
        [llvm_dist / "bin/llvm-ar", "t", archive],
        f"could not inspect target runtime archive {archive.name}",
    ).stdout.decode().splitlines()
    if members != expected_members:
        raise SystemExit(
            f"target runtime archive has unstable members: {archive}: {members}"
        )
    for member in members:
        contents = run_checked(
            [llvm_dist / "bin/llvm-ar", "p", archive, member],
            f"could not extract {member} from {archive.name}",
        ).stdout
        if bitcode:
            output = archive_inspection / f"{archive.name}-{member}.ll"
            parser = [llvm_dist / "bin/llvm-dis", "-o", output, "-"]
        else:
            parser = [llvm_dist / "bin/llvm-readobj", "--file-headers", "-"]
        parsed = subprocess.run(
            [str(item) for item in parser],
            input=contents,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        if parsed.returncode:
            sys.stdout.buffer.write(parsed.stdout)
            kind = "LLVM bitcode" if bitcode else "ELF"
            raise SystemExit(f"{archive.name} member {member} is not {kind}")


def verify_staged_archives():
    for archive in (runtime_archive, runtime_lto_archive):
        staged = support / archive.name
        if not staged.is_file() or staged.read_bytes() != archive.read_bytes():
            raise SystemExit(f"native support did not stage {archive.name}")
    complete = (support / ".complete").read_text().splitlines()
    if len(complete) != 3 or len(complete[2]) != 64:
        raise SystemExit("native-support completion record has no content hash")


inspect_archive(runtime_archive, native_members, bitcode=False)
inspect_archive(runtime_lto_archive, lto_members, bitcode=True)
verify_staged_archives()

# An unchanged second build is a true graph no-op.
build_graph(graph_build, "obelisk_native_support")
if fingerprint(graph_sysroot_stamps[0]) != first_sysroot:
    raise SystemExit("second graph build reprovisioned the target sysroot")
if fingerprint(runtime_archive) != first_runtime:
    raise SystemExit("second graph build rebuilt the target runtime")
if fingerprint(runtime_lto_archive) != first_runtime_lto:
    raise SystemExit("second graph build rebuilt the Full-LTO target runtime")
if fingerprint(support_stamps[0]) != first_support:
    raise SystemExit("second graph build restaged native support")

# A runtime-only source edit rebuilds the archive and staged support, but not
# the sysroot. Recreating the archive must also retain deterministic members.
with (graph_runtime / "lib/Runtime.cpp").open("a") as stream:
    stream.write("\n// isolated build-graph rebuild probe\n")
build_graph(graph_build, "obelisk_native_support")
if fingerprint(graph_sysroot_stamps[0]) != first_sysroot:
    raise SystemExit("runtime-only change reprovisioned the target sysroot")
if fingerprint(runtime_archive) == first_runtime:
    raise SystemExit("runtime-only change did not rebuild the runtime archive")
if fingerprint(runtime_lto_archive) == first_runtime_lto:
    raise SystemExit(
        "runtime-only change did not rebuild the Full-LTO runtime archive"
    )
if fingerprint(support_stamps[0]) == first_support:
    raise SystemExit("runtime-only change did not restage native support")
inspect_archive(runtime_archive, native_members, bitcode=False)
inspect_archive(runtime_lto_archive, lto_members, bitcode=True)
verify_staged_archives()

# Public and internal runtime headers are also dependencies of both archive
# forms and of the staged content-hash command.
source_runtime = fingerprint(runtime_archive)
source_runtime_lto = fingerprint(runtime_lto_archive)
source_support = fingerprint(support_stamps[0])
with (graph_runtime / "include/obelisk/Runtime/Runtime.h").open("a") as stream:
    stream.write("\n// isolated build-graph header rebuild probe\n")
build_graph(graph_build, "obelisk_native_support")
if fingerprint(graph_sysroot_stamps[0]) != first_sysroot:
    raise SystemExit("runtime-header change reprovisioned the target sysroot")
if fingerprint(runtime_archive) == source_runtime:
    raise SystemExit("runtime-header change did not rebuild the runtime archive")
if fingerprint(runtime_lto_archive) == source_runtime_lto:
    raise SystemExit(
        "runtime-header change did not rebuild the Full-LTO runtime archive"
    )
if fingerprint(support_stamps[0]) == source_support:
    raise SystemExit("runtime-header change did not restage native support")
verify_staged_archives()

# Missing declared byproducts cause the staging command to self-heal and
# atomically move the public relative link to a complete version.
(support / "README.txt").unlink()
build_graph(graph_build, "obelisk_native_support")
if not (support / "README.txt").is_file():
    raise SystemExit("staged native-support byproduct did not self-heal")
published = pathlib.Path(os.readlink(support))
if published.is_absolute():
    raise SystemExit("published native-support link is not relocatable")

# A layout change selects a new content-addressed sysroot.
configure_graph(graph_source, graph_build, graph_runtime, layout=2)
build_graph(graph_build, "obelisk_target_sysroot")
if len(list((graph_build / "target-sysroots").glob("*/.complete"))) < 2:
    raise SystemExit("layout change did not select a new target sysroot key")

# Pre-extracted contents are configure inputs. Editing one file triggers an
# automatic reconfigure and a new key; an escaping link cannot receive a stamp.
pre_source = scratch / "pre-graph-source"
pre_build = scratch / "pre-graph-build"
pre_source.mkdir()
(pre_source / "CMakeLists.txt").write_text(
    "\n".join(
        (
            "cmake_minimum_required(VERSION 3.20)",
            "project(ObeliskPreextractedGraphTest LANGUAGES NONE)",
            f'set(OBELISK_SOURCE_DIR [[{source_root}]])',
            "set(OBELISK_TARGET_SYSROOT_ONLY ON)",
            f'include([[{source_root / "cmake/TargetNativeSupport.cmake"}]])',
            "",
        )
    )
)
configure_graph(pre_source, pre_build, preextracted=sysroot)
build_graph(pre_build, "obelisk_target_sysroot")
pre_stamps = list((pre_build / "target-sysroots").glob("*/.complete"))
with (sysroot / "usr/share/doc/libc6/copyright").open("a") as stream:
    stream.write("\nObelisk build-graph fingerprint probe.\n")
build_graph(pre_build, "obelisk_target_sysroot")
if len(list((pre_build / "target-sysroots").glob("*/.complete"))) <= len(pre_stamps):
    raise SystemExit("pre-extracted content edit did not select a new key")

escape = sysroot / "obelisk-escape-probe"
escape.symlink_to("/obelisk-outside-sysroot")
before_bad = len(list((pre_build / "target-sysroots").glob("*/.complete")))
build_graph(pre_build, "obelisk_target_sysroot", expect_success=False)
after_bad = len(list((pre_build / "target-sysroots").glob("*/.complete")))
if after_bad != before_bad:
    raise SystemExit("escaping pre-extracted symlink produced a completion stamp")
