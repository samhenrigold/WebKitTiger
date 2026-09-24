#!/usr/bin/env python3
"""Serialized, fail-closed Mini builds. No Ninja pipeline or stale bin/ download.

The Mini lease covers source/dependency synchronization through publication.
Existing unmanaged Ninja jobs must finish before synchronization. Installed sysroots
are the dependency inputs: this synchronizes and hashes them, it does not rebuild
dependencies or infer that a changed compat source was installed.
"""
import contextlib
import hashlib
import json
import os
from pathlib import Path
import re
import select
import shlex
import subprocess
import sys
import tempfile
import uuid


class BuildError(RuntimeError):
    pass


def digest(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def run(args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


def capture(args, **kwargs):
    return subprocess.check_output(args, text=True, **kwargs).strip()


def git_identity(path, outer=False):
    head = capture(["git", "-C", str(path), "rev-parse", "HEAD"])
    h = hashlib.sha256(subprocess.check_output(["git", "-C", str(path), "diff", "--binary", "HEAD", "--"]))
    # Outer untracked worktrees/logs are not compile inputs. Exact synchronized
    # dependency inputs get their own hashes, including untracked helper tools.
    if not outer:
        files = subprocess.check_output(["git", "-C", str(path), "ls-files", "--others", "--exclude-standard", "-z"])
        for name in sorted(files.split(b"\0")):
            if name:
                p = path / os.fsdecode(name)
                h.update(name + b"\0")
                h.update(os.readlink(p).encode() if p.is_symlink() else bytes.fromhex(digest(p)))
    result = {"head": head, "dirty_sha256": h.hexdigest()}
    if not outer:
        result["tree"] = capture(["git", "-C", str(path), "rev-parse", "HEAD^{tree}"])
    return result


def inventory(root, paths):
    """Hash regular-file contents and symlink targets, without following SDK links."""
    result = {}
    for name in paths:
        start = root / name
        if not start.exists() and not start.is_symlink():
            raise BuildError("missing required build input: " + str(start))
        entries = [start]
        if start.is_dir() and not start.is_symlink():
            entries.extend(sorted(start.rglob("*")))
        for p in entries:
            # Finder rewrites this metadata independently on each Mac. It is
            # never a compiler input and cannot be part of a frozen identity.
            if p.name == ".DS_Store":
                continue
            if p.is_symlink():
                result[str(p.relative_to(root))] = "link:" + os.readlink(p)
            elif p.is_file():
                result[str(p.relative_to(root))] = digest(p)
    return result


def json_hash(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def read_cache(path):
    result = {}
    if path.exists():
        for line in path.read_text().splitlines():
            match = re.match(r"([^/#:][^:]*):[^=]+=(.*)", line)
            if match:
                result[match[1]] = match[2]
    return result


def validate_cache(cache, source, process, toolchain):
    wanted = {"CMAKE_HOME_DIRECTORY": str(source), "TIGER_PROCESS": process,
              "CMAKE_TOOLCHAIN_FILE": str(toolchain)}
    for key, value in wanted.items():
        if key in cache and cache[key] != value:
            raise BuildError("build directory identity mismatch: %s=%r, expected %r; use a different build name" % (key, cache[key], value))


def logged(args, log, env=None, stdout_path=None):
    display = shlex.join([str(a) for a in args])
    if len(display) > 300:
        display = str(args[0]) + " (" + str(len(args) - 1) + " arguments; full command in log)"
    print("mini-build: " + display + "; log: " + str(log), flush=True)
    with open(log, "w") as output:
        output.write("$ " + shlex.join([str(a) for a in args]) + "\n")
        output.flush()
        if stdout_path:
            with open(stdout_path, "w") as stdout:
                proc = subprocess.run(args, stdout=stdout, stderr=output, env=env)
        else:
            proc = subprocess.run(args, stdout=output, stderr=subprocess.STDOUT, env=env)
    if proc.returncode:
        lines = Path(log).read_text(errors="replace").splitlines()
        print("\n".join(lines[-60:]), file=sys.stderr, flush=True)
        raise BuildError("command failed (%d); full log: %s" % (proc.returncode, log))


def artifact_paths(build, targets):
    result = []
    for target in targets:
        # The injected WKTR target deliberately uses the regular helper's basename.
        if target == "TigerWebProcessTests":
            candidates = [build / "bin/wktr/TigerWebProcess"]
        else:
            candidates = (build / "bin").rglob(target)
        matches = [p for p in candidates if p.is_file() and not p.is_symlink()]
        if len(matches) != 1:
            raise BuildError("expected exactly one executable for target %s, found %d" % (target, len(matches)))
        if not os.access(matches[0], os.X_OK):
            raise BuildError("target is not executable: " + str(matches[0]))
        result.append(matches[0])
    return result


def remote_build(job):
    root = Path(job["root"])
    build = root / "build" / job["build"]
    source = root / job["worktree"]
    build.mkdir(parents=True, exist_ok=True)
    # Old executables remain useful Ninja inputs, but a failed job exports nothing.
    (build / "build-manifest.json").unlink(missing_ok=True)
    for name in ("mini-configure.log", "mini-ninja.log", "mini-wire.log", "wire-fingerprints.json", "wire-messages.txt", "wire-serializers.txt"):
        (build / name).unlink(missing_ok=True)
    actual = inventory(root, job["input_paths"])
    if actual != job["inputs"]:
        raise BuildError("Mini dependency/toolchain content differs from synchronized input inventory")
    toolchain = root / "toolchain" / job["toolchain"]
    validate_cache(read_cache(build / "CMakeCache.txt"), source, job["process"], toolchain)
    env = dict(os.environ, PATH=str(root / "bin") + ":" + str(root / "cmake/bin") + ":" + os.environ.get("PATH", ""))
    cmake = [str(root / "cmake/bin/cmake"), "-G", "Ninja", "-S", str(source), "-B", str(build)]
    cmake += job["options"] + job["extra_cmake"]
    cmake += ["-DWKT:PATH=" + str(root), "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=" + str(toolchain),
              "-DTIGER_PROCESS:STRING=" + job["process"], "-DCMAKE_MAKE_PROGRAM=" + str(root / "bin/ninja"),
              "-DPKG_CONFIG_EXECUTABLE=" + str(root / "bin/pkg-config"), "-DPython_EXECUTABLE=/usr/bin/python3",
              "-DRUBY_EXECUTABLE=/usr/bin/ruby", "-DCCACHE_FOUND:FILEPATH=" + str(root / "bin/ccache")]
    logged(cmake, build / "mini-configure.log", env)
    validate_cache(read_cache(build / "CMakeCache.txt"), source, job["process"], toolchain)
    logged([str(root / "bin/ninja"), "-C", str(build), "-j" + str(job["jobs"])] + job["targets"], build / "mini-ninja.log", env)
    artifacts = artifact_paths(build, job["targets"])
    wire_out = build / "wire-fingerprints.json"
    logged(["/usr/bin/python3", str(root / "tools/tiger-artifacts.py"), "fingerprint", "--build-dir", str(build), "--out", str(build)], build / "mini-wire.log", env, wire_out)
    wire = json.loads(wire_out.read_text())
    for key, filename in (("messages_sha256", "wire-messages.txt"), ("serializers_sha256", "wire-serializers.txt")):
        if wire.get(key) != digest(build / filename):
            raise BuildError("invalid wire fingerprint: " + key)
    manifest = {"schema": 1, "source": job["source"], "outer": job["outer"], "process": job["process"],
                "configuration_sha256": digest(build / "CMakeCache.txt"), "configuration": digest(build / "CMakeCache.txt"),
                "dependencies_sha256": json_hash(actual), "inputs": actual,
                "binaries": {str(p.relative_to(build / "bin")): digest(p) for p in artifacts}, "wire": wire,
                "build_name": job["build"], "worktree": job["worktree"], "build_id": job["id"],
                "configure_command": cmake, "targets": job["targets"]}
    (build / "build-manifest.json").write_text(json.dumps(manifest, sort_keys=True, indent=2) + "\n")


@contextlib.contextmanager
def mini_lock(host, root, timeout):
    """Hold the remote lease via SSH stdin, including while rsync runs locally."""
    lock = str(root / "build/.mini-build.lock")
    token = uuid.uuid4().hex
    script = """set -eu
lock=%s
mkdir -p %s
deadline=$(( $(date +%%s) + %s ))
until mkdir "$lock" 2>/dev/null; do
    [ "$(date +%%s)" -lt "$deadline" ] || exit 1
    echo 'mini-build: waiting for Mini lease' >&2; sleep 1
done
trap 'rm -f "$lock/owner"; rmdir "$lock"' EXIT
trap 'exit 1' HUP INT TERM
printf '%%s\\n' %s > "$lock/owner"
while pgrep -x ninja >/dev/null; do echo 'mini-build: waiting for legacy Ninja before syncing' >&2; sleep 5; done
while [ "$(sysctl -n vm.loadavg | awk '{print int($2)}')" -ge 12 ]; do
    echo 'mini-build: waiting for Mini load below 12' >&2; sleep 10
done
echo MINI_BUILD_LOCKED
cat >/dev/null
""" % (shlex.quote(lock), shlex.quote(str(root / "build")), max(1, int(timeout)), shlex.quote(token))
    proc = subprocess.Popen(["ssh", host, "/bin/sh -c " + shlex.quote(script)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    try:
        ready, _, _ = select.select([proc.stdout], [], [], timeout)
        if not ready or proc.stdout.readline().strip() != "MINI_BUILD_LOCKED":
            raise BuildError("Mini lock was not acquired; no inputs synchronized")
        yield proc
        if proc.poll() is not None:
            raise BuildError("Mini lease connection was lost")
    finally:
        if proc.stdin:
            proc.stdin.close()
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.terminate()
            proc.wait(timeout=10)
        proc.stdout.close()


def ssh(host, args):
    return run(["ssh", host, shlex.join([str(x) for x in args])])


def sync_path(host, root, relative, directory=False, excludes=()):
    local = root / relative
    ssh(host, ["mkdir", "-p", str(root / relative.parent)])
    # Changed content gets a fresh mtime on the Mini. Preserving a restored old
    # source/archive timestamp can otherwise hide a change from Ninja's mtime DAG.
    args = ["rsync", "-a", "--checksum", "--no-times", "--exclude", ".DS_Store"]
    if directory:
        args += ["--delete"]
    for exclusion in excludes:
        args += ["--exclude", exclusion]
    args += [str(local) + ("/" if directory else ""), host + ":" + shlex.quote(str(root / relative) + ("/" if directory else ""))]
    run(args)


def publish(download, destination):
    manifest = json.loads((download / "build-manifest.json").read_text())
    for name, expected in manifest["binaries"].items():
        p = Path(name)
        if p.is_absolute() or ".." in p.parts or digest(download / "bin" / p) != expected:
            raise BuildError("downloaded binary hash/path mismatch: " + name)
    for key, name in (("messages_sha256", "wire-messages.txt"), ("serializers_sha256", "wire-serializers.txt")):
        if digest(download / name) != manifest["wire"][key]:
            raise BuildError("downloaded wire data mismatch: " + name)
    # Manifest last: a concurrent verifier fails closed during publication.
    destination.mkdir(parents=True, exist_ok=True)
    (destination / "build-manifest.json").unlink(missing_ok=True)
    for name in manifest["binaries"]:
        target = destination / "bin" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        os.replace(download / "bin" / name, target)
    for name in ("wire-messages.txt", "wire-serializers.txt", "build-manifest.json"):
        os.replace(download / name, destination / name)


def input_paths(root):
    # Sync installed dependencies, not multi-gigabyte dependency/LLVM source trees.
    # Keep mutable screenshots, fixture downloads, lock files and .o files out of
    # both the Mini mirrors and the input identity.
    paths = ["toolchain/bin", "toolchain/llvm-tiger", "toolchain/cctools", "toolchain/sysroot-i386", "toolchain/sysroot-x86_64",
             "toolchain/tiger.cmake", "toolchain/tiger64.cmake", "sdk/MacOSX10.4u.sdk",
             "compat/include", "compat/sdk-overlay", "compat/dispatch/include",
             "spike/CAHost/Frameworks", "build/builtins-i386/libclang_rt.builtins-i386.a",
             "build/web-opts.txt", "build/ui-opts.txt", "tools/mini-build.py", "tools/tiger-artifacts.py", "tools/tiger-wire-schema.py"]
    for pattern in ("compat/*.c", "compat/*.m", "compat/*.mm", "compat/dispatch/*.c", "deps/build-*.sh",
                    "spike/*.h", "spike/msescan.c", "spike/media/*.h", "spike/wk2web/*.h", "spike/wk2web/*.mm", "spike/wk2web/*.cpp"):
        paths.extend(str(p.relative_to(root)) for p in sorted(root.glob(pattern)) if p.is_file())
    return sorted(set(paths))


def main(argv):
    if argv and argv[0] == "--remote":
        remote_build(json.loads(Path(argv[1]).read_text()))
        return
    if len(argv) < 2:
        raise BuildError("usage: mini-build.sh <worktree> <build-name> <targets...>")
    worktree, build_name = argv[:2]
    targets = argv[2:] or ["TigerWebProcess", "TigerNetworkProcess"]
    for name in [worktree, build_name] + targets:
        if not re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_.+-]*", name):
            raise BuildError("unsafe or unsupported name: " + name)
    if worktree == "WebKit":
        raise BuildError("use an isolated WebKit worktree; Mini's WebKit/ mirror is not a build target")
    root = Path(os.environ.get("WKT", "/Users/shg/Developer/WebKitTiger")).resolve()
    host = os.environ.get("MINI", "shg@shg-mini.local")
    process = os.environ.get("TIGER_PROCESS") or ("GPU" if "gpu" in build_name else "UI" if "ui" in build_name else "WEB")
    if process not in ("WEB", "UI", "GPU", "NETWORK"):
        raise BuildError("invalid TIGER_PROCESS")
    i386 = process in ("UI", "GPU")
    options_path = root / "build" / ("ui-opts.txt" if i386 else "web-opts.txt")
    options = [line for line in options_path.read_text().splitlines() if line.startswith("-D") and "_EXECUTABLE:" not in line and not line.startswith("-DTIGER_PROCESS:")]
    paths = input_paths(root)
    job = {"root": str(root), "worktree": worktree, "build": build_name, "targets": targets,
           "process": process, "toolchain": "tiger.cmake" if i386 else "tiger64.cmake", "jobs": int(os.environ.get("J", "8")),
           "options": options, "extra_cmake": shlex.split(os.environ.get("EXTRA_CMAKE", "")),
           "id": uuid.uuid4().hex, "input_paths": paths}
    if job["jobs"] < 1:
        raise BuildError("J must be positive")
    destination = root / "build" / build_name
    logs = root / "logs/mini-build" / job["id"]
    logs.mkdir(parents=True, exist_ok=True)
    with mini_lock(host, root, float(os.environ.get("MINI_LOCK_TIMEOUT", "7200"))) as lease:
        print("mini-build: lease acquired; hashing installed inputs", flush=True)
        job["source"] = git_identity(root / worktree)
        job["outer"] = git_identity(root, outer=True)
        job["inputs"] = inventory(root, paths)
        sync_path(host, root, Path(worktree), True, (".git", "LayoutTests", "WebKitBuild", "PerformanceTests", "Websites"))
        for path in paths:
            sync_path(host, root, Path(path), (root / path).is_dir())
        if git_identity(root / worktree) != job["source"]:
            raise BuildError("source changed during synchronization; rerun after edits settle")
        with tempfile.TemporaryDirectory(prefix="mini-build-", dir=root / "build") as temporary:
            temp = Path(temporary)
            jobfile = temp / "job.json"
            jobfile.write_text(json.dumps(job))
            remote_job = root / "build" / (".mini-job-" + job["id"] + ".json")
            run(["rsync", "-a", str(jobfile), host + ":" + shlex.quote(str(remote_job))])
            try:
                ssh(host, ["/usr/bin/python3", str(root / "tools/mini-build.py"), "--remote", str(remote_job)])
            finally:
                # Preserve full logs even when configure, Ninja or wire verification fails.
                for name in ("mini-configure.log", "mini-ninja.log", "mini-wire.log", "wire-fingerprints.json"):
                    subprocess.run(["rsync", "-a", host + ":" + shlex.quote(str(destination / name)), str(logs / name)], check=False)
                ssh(host, ["rm", "-f", str(remote_job)])
            run(["rsync", "-a", host + ":" + shlex.quote(str(destination / "build-manifest.json")), str(temp / "build-manifest.json")])
            manifest = json.loads((temp / "build-manifest.json").read_text())
            if manifest["build_id"] != job["id"] or manifest["source"] != job["source"]:
                raise BuildError("remote manifest is stale or belongs to another source")
            for name in manifest["binaries"]:
                p = Path(name)
                if p.is_absolute() or ".." in p.parts:
                    raise BuildError("unsafe artifact path: " + name)
            for relative in [Path("bin") / name for name in manifest["binaries"]] + [Path("wire-messages.txt"), Path("wire-serializers.txt")]:
                local = temp / relative
                local.parent.mkdir(parents=True, exist_ok=True)
                run(["rsync", "-a", host + ":" + shlex.quote(str(destination / relative)), str(local)])
            if git_identity(root / worktree) != job["source"] or inventory(root, paths) != job["inputs"]:
                raise BuildError("local source/dependencies changed during the build; artifacts not published")
            if lease.poll() is not None:
                raise BuildError("Mini lease lost; artifacts not published")
            publish(temp, destination)
    print("mini-build: verified artifacts in %s; complete logs in %s" % (destination, logs))


if __name__ == "__main__":
    try:
        main(sys.argv[1:])
    except (BuildError, subprocess.CalledProcessError, OSError, ValueError) as error:
        print("mini-build: " + str(error), file=sys.stderr)
        sys.exit(1)
