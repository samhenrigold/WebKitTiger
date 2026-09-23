#!/usr/bin/env python3
"""Matched-artifact staging/install, with a Tiger lease and scoped process cleanup."""
import argparse
import contextlib
import fcntl
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import select
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
REMOTE_BASE = "/Users/shg/wk2"
REMOTE_APPLICATIONS = "/Users/shg/Applications"
APPS = ("TigerWK2App", "TigerBrowser2")
NAME = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
BUSY = re.compile(r"(?:^|[/\s])(TigerWK2App|TigerBrowser2|TigerWebProcess|TigerNetworkProcess|TigerGPUProcess|tigeraudio32|pagedriver)(?:\s|$)|/Applications/TigerBrowser\.app/")


class RunError(Exception):
    pass


def checked(argv, **kwargs):
    return subprocess.run([str(arg) for arg in argv], check=True, **kwargs)


def parse_app_env(value):
    """Historical assignment/optional-wrapper syntax, with no shell evaluation."""
    assignments, wrapper = {}, []
    for word in shlex.split(value):
        if not wrapper and "=" in word and NAME.fullmatch(word.split("=", 1)[0]):
            key, val = word.split("=", 1)
            assignments[key] = val
        else:
            wrapper.append(word)
    return assignments, wrapper


def build_dir(root, env, name, default):
    path = Path(env.get(name, default)).expanduser()
    return path.resolve() if path.is_absolute() else (root / path).resolve()


def artifacts(root, env, verifier=checked):
    """Fail locally before a lease, mkdir, transfer, or any remote probe."""
    ui = build_dir(root, env, "UIDIR", "build/tiger-ui-port")
    web = build_dir(root, env, "WEBDIR", "build/tiger-web-port")
    gpu = build_dir(root, env, "GPUDIR", "build/tiger-gpu")
    binaries = [ui / "bin" / name for name in APPS]
    binaries += [web / "bin/TigerWebProcess", web / "bin/TigerNetworkProcess",
                 gpu / "bin/TigerGPUProcess", root / "build/tigeraudio32"]
    resources = [root / "logs/tiger-fonts.json", root / "deps/src/cacert.pem"]
    framework = root / "spike/CAHost/Frameworks/QuartzCore.framework"
    for path in binaries + resources + [framework]:
        if not path.exists():
            raise RunError("missing required artifact: %s" % path)
    for path in binaries:
        if not path.is_file() or not os.access(path, os.X_OK):
            raise RunError("required binary is not executable: %s" % path)
    if not framework.is_dir():
        raise RunError("QuartzCore framework is not a directory: %s" % framework)
    if not (framework / "QuartzCore").is_file():
        raise RunError("missing required framework binary: %s" % (framework / "QuartzCore"))
    verifier([sys.executable, root / "tools/tiger-artifacts.py", "verify",
              "--ui", ui, "--web", web, "--gpu", gpu])
    return binaries, resources, framework


@contextlib.contextmanager
def frozen_artifacts(root, env, verifier):
    """Copy and verify the copy, so a concurrent build cannot change staged bytes."""
    binaries, resources, framework = artifacts(root, env, verifier)
    with tempfile.TemporaryDirectory(prefix="tiger-candidate-") as temp:
        snapshot = Path(temp)
        frozen = []
        directories = {}
        for name, default, names in (
            ("UIDIR", "build/tiger-ui-port", APPS),
            ("WEBDIR", "build/tiger-web-port", ("TigerWebProcess", "TigerNetworkProcess")),
            ("GPUDIR", "build/tiger-gpu", ("TigerGPUProcess",)),
        ):
            source = build_dir(root, env, name, default)
            target = snapshot / name
            (target / "bin").mkdir(parents=True)
            for executable in names:
                path = target / "bin" / executable
                shutil.copy2(source / "bin" / executable, path)
                frozen.append(path)
            for metadata in ("build-manifest.json", "wire-messages.txt", "wire-serializers.txt"):
                shutil.copy2(source / metadata, target / metadata)
            directories[name] = target
        verifier([sys.executable, root / "tools/tiger-artifacts.py", "verify",
                  "--ui", directories["UIDIR"], "--web", directories["WEBDIR"], "--gpu", directories["GPUDIR"]])
        audio = snapshot / "tigeraudio32"
        shutil.copy2(binaries[-1], audio)
        frozen.append(audio)
        frozen_resources = []
        for source in resources:
            target = snapshot / source.name
            shutil.copy2(source, target)
            frozen_resources.append(target)
        frozen_framework = snapshot / framework.name
        shutil.copytree(framework, frozen_framework, symlinks=True)
        yield frozen, frozen_resources, frozen_framework


@contextlib.contextmanager
def local_lock(root, timeout):
    # Same inode as the original shell's fd-9 lock; older scripts wait too.
    with (root / "spike/wk2web/box.lock").open("a") as lock:
        deadline = time.monotonic() + timeout
        while True:
            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except BlockingIOError:
                if time.monotonic() >= deadline:
                    raise RunError("box busy: local lease timed out")
                time.sleep(min(1, max(0, deadline - time.monotonic())))
        yield


LEASE_PROGRAM = r'''
use strict;
use Fcntl qw(:flock);
$| = 1;
my ($path, $timeout, $token) = @ARGV;
open(my $lock, '>>', $path) or die "lease open: $!\n";
my $end = time + $timeout;
while (!flock($lock, LOCK_EX | LOCK_NB)) {
    die "box busy: remote lease timed out\n" if time >= $end;
    sleep 1;
}
seek($lock, 0, 0); truncate($lock, 0);
print $lock "$token $$\n";
print "TIGER-LEASE $token\n";
# A lost TCP connection need not produce EOF promptly. Only the owner renews this
# watchdog; nobody else removes a lock or guesses whether its timestamp is stale.
$SIG{ALRM} = sub { die "lease heartbeat expired\n"; };
alarm 90;
while (<STDIN>) { alarm 90; }
'''


class Remote:
    def __init__(self, host):
        if not re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_.@:-]*", host):
            raise RunError("invalid TIGER_HOST")
        self.host = host
        self.lease_process = None

    def check_lease(self):
        if self.lease_process is not None and self.lease_process.poll() is not None:
            raise RunError("remote lease connection was lost; refusing further staging")

    def command(self, argv):
        return ["ssh", "-o", "ConnectTimeout=15", "-o", "ServerAliveInterval=15",
                "-o", "ServerAliveCountMax=3", self.host, shlex.join([str(arg) for arg in argv])]

    def run(self, argv, **kwargs):
        # Cleanup targets only the owned run even after losing the lease connection.
        if "cleanup" not in argv:
            self.check_lease()
        return checked(self.command(argv), **kwargs)

    def transfer(self, sources, destination):
        self.check_lease()
        # No --inplace: executable destinations are unique to this run.
        checked(["rsync", "-rtl", "-z", "--bwlimit=20000", "--"] +
                list(sources) + [self.host + ":" + shlex.quote(destination)])

    def receive(self, source, destination):
        Path(destination).parent.mkdir(parents=True, exist_ok=True)
        checked(["scp", "-qO", self.host + ":" + shlex.quote(source), destination])

    @contextlib.contextmanager
    def lease(self, token, timeout):
        # Kernel flock releases on connection close, including an interrupted owner.
        # No stale timestamp can steal a live lease.
        proc = subprocess.Popen(self.command(["perl", "-e", LEASE_PROGRAM,
                                              "/Users/shg/.webkittiger-box.lock", str(timeout), token]),
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        stop_heartbeat = threading.Event()
        heartbeat = None
        try:
            ready, _, _ = select.select([proc.stdout], [], [], timeout + 20)
            line = proc.stdout.readline().strip() if ready else ""
            if line != "TIGER-LEASE " + token:
                raise RunError("box busy or remote lease unavailable; nothing launched")
            self.lease_process = proc
            def renew():
                while not stop_heartbeat.wait(15):
                    try:
                        proc.stdin.write("keep\n")
                        proc.stdin.flush()
                    except (BrokenPipeError, OSError, ValueError):
                        return
            heartbeat = threading.Thread(target=renew, name="tiger-lease", daemon=True)
            heartbeat.start()
            yield
            if proc.poll() is not None:
                raise RunError("remote lease connection was lost")
        finally:
            self.lease_process = None
            stop_heartbeat.set()
            if heartbeat is not None:
                heartbeat.join(timeout=2)
            try:
                proc.stdin.close()
            except BrokenPipeError:
                pass
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.terminate()
                proc.wait(timeout=10)
            proc.stdout.close()


def wait_idle(remote, timeout):
    deadline = time.monotonic() + timeout
    reported = False
    while True:
        # Tiger's ps drops commands for the modern "pid=,command=" spelling.
        table = remote.run(["ps", "-axww", "-o", "pid,command"], capture_output=True, text=True).stdout
        busy = [line.strip() for line in table.splitlines() if BUSY.search(line)]
        if not busy:
            return
        if not reported:
            print("waiting: box occupied (nothing will be killed):\n" + "\n".join(busy[:5]), flush=True)
            reported = True
        if time.monotonic() >= deadline:
            raise RunError("box still busy; user app and other processes left untouched")
        time.sleep(min(5, max(0, deadline - time.monotonic())))


def run_environment(env, run_path):
    extra, wrapper = parse_app_env(env.get("APP_ENV", ""))
    values = {
        "HOME": run_path + "/home" if env.get("RUN_HOME_NEW") == "1" else REMOTE_BASE + "/home",
        "TIGER_FONT_MANIFEST": REMOTE_BASE + "/share/tiger-fonts.json",
        "TIGER_CA_BUNDLE": REMOTE_BASE + "/share/cacert.pem",
    }
    values.update(extra)
    # Cookie tests deliberately share /wk2/cookiehome between two launches.
    home = PurePosixPath(values["HOME"])
    if ".." in home.parts or not home.is_absolute() or REMOTE_BASE not in [str(p) for p in home.parents]:
        raise RunError("HOME must be an isolated directory below " + REMOTE_BASE)
    if str(home) in (REMOTE_BASE + "/share", REMOTE_BASE + "/bin", REMOTE_BASE + "/Frameworks", REMOTE_BASE + "/runs"):
        raise RunError("HOME must be a profile directory, not a staging directory")
    values["WEBKIT_TIGER_HELPER_DIR"] = run_path + "/bin"
    values["TIGER_RUN_ID"] = run_path.rsplit("/", 1)[1]
    return values, wrapper


def provenance(payload, destination):
    """Retain the exact manifests/wire evidence next to each delivered candidate."""
    for label, binary in (("UI", payload[0][0]), ("WEB", payload[0][2]), ("GPU", payload[0][4])):
        target = destination / label
        target.mkdir(parents=True, exist_ok=True)
        for filename in ("build-manifest.json", "wire-messages.txt", "wire-serializers.txt"):
            shutil.copy2(binary.parent.parent / filename, target / filename)
    # These runtime assets are frozen too, but are not engine build products.
    # Record their exact delivered bytes without claiming source/build provenance.
    assets = {}
    binaries, resources, framework = payload
    paths = [(path.name, path) for path in [binaries[-1]] + resources]
    paths += [(framework.name + "/" + str(path.relative_to(framework)), path)
              for path in sorted(framework.rglob("*")) if path.is_file() or path.is_symlink()]
    for name, path in paths:
        if path.is_symlink():
            assets[name] = {"symlink": os.readlink(path)}
            continue
        fingerprint = hashlib.sha256()
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                fingerprint.update(block)
        assets[name] = {"sha256": fingerprint.hexdigest()}
    (destination / "runtime-assets.json").write_text(json.dumps(assets, sort_keys=True, indent=2) + "\n")


def stage(root, env, url, seconds, remote, verifier=checked):
    with frozen_artifacts(root, env, verifier) as payload:
        return stage_frozen(root, env, url, seconds, remote, payload)


def stage_frozen(root, env, url, seconds, remote, payload):
    app = env.get("APP", "TigerWK2App")
    if app not in APPS:
        raise RunError("APP must be TigerWK2App or TigerBrowser2")
    if seconds < 1 or seconds > 86400:
        raise RunError("seconds must be between 1 and 86400")
    binaries, resources, framework = payload
    token = time.strftime("%Y%m%d-%H%M%S-") + uuid.uuid4().hex[:12]
    run_path = REMOTE_BASE + "/runs/" + token
    values, wrapper = run_environment(env, run_path)
    files = resources + sorted((root / "spike/wk2web").glob("*.html"))
    if wrapper and wrapper[0] == REMOTE_BASE + "/share/benchstamp.pl":
        files.append(root / "spike/bench/benchstamp.pl")
    ca_bundle = Path(env["STAGE_CA_BUNDLE"]).resolve() if env.get("STAGE_CA_BUNDLE") else None
    if ca_bundle and not ca_bundle.is_file():
        raise RunError("missing STAGE_CA_BUNDLE: %s" % ca_bundle)
    for path in files:
        if not path.is_file():
            raise RunError("missing stage resource: %s" % path)
    local_out = root / "build/runs" / token
    shot = Path(env.get("SHOT", str(local_out / "shot.png"))).resolve()
    log = Path(env.get("LOG") or str(local_out / "app.log")).resolve()
    timeout = int(env.get("LOCK_TIMEOUT", "600"))
    provenance(payload, local_out / "provenance")
    with local_lock(root, timeout), remote.lease(token, timeout):
        wait_idle(remote, int(env.get("BUSY_TIMEOUT", "300")))
        remote.run(["mkdir", "-p", run_path + "/bin", run_path + "/Frameworks",
                    REMOTE_BASE + "/share", values["HOME"]])
        remote.transfer(files, REMOTE_BASE + "/share/")
        if ca_bundle:
            remote.transfer([ca_bundle], REMOTE_BASE + "/share/cookie-bundle.pem")
        remote.transfer(binaries, run_path + "/bin/")
        remote.transfer([framework], run_path + "/Frameworks/")
        remote.transfer([root / "tools/tiger-run-remote.pl"], run_path + "/")
        remote.transfer([local_out / "provenance"], run_path + "/")
        remote.run(["test", "-r", values["TIGER_CA_BUNDLE"]])
        remote.run(["test", "-r", values["TIGER_FONT_MANIFEST"]])
        print("run: " + run_path, flush=True)
        argv = ["perl", run_path + "/tiger-run-remote.pl", "run", run_path, str(seconds), "env"]
        argv += [key + "=" + value for key, value in values.items()]
        argv += wrapper + [run_path + "/bin/" + app, url, str(seconds), "-WebKitLogging", "Process,Loading"]
        failure = None
        try:
            remote.run(argv)
        except BaseException as error:
            failure = error
        # Preserve the original failure and still recover every available diagnostic.
        for operation in (
            lambda: remote.run(["perl", run_path + "/tiger-run-remote.pl", "cleanup", run_path]),
            lambda: remote.receive(run_path + "/app.log", log),
            lambda: remote.receive(run_path + "/shot.png", shot),
        ):
            try:
                operation()
            except (OSError, RunError, subprocess.CalledProcessError) as error:
                print("tiger-run: cleanup/diagnostic: " + str(error), file=sys.stderr)
                if failure is None:
                    failure = error
        if failure is not None:
            raise failure
        print("== app.log", flush=True)
        print("\n".join(log.read_text(errors="replace").splitlines()[-40:]))
        print("screenshot: %s\nlog: %s" % (shot, log))


LAUNCHER = '''#!/bin/sh
DIR=$(cd "$(dirname "$0")" && pwd)
RES="$DIR/../Resources"
export TIGER_FONT_MANIFEST="$RES/tiger-fonts.json"
export TIGER_CA_BUNDLE="$RES/cacert.pem"
export WEBKIT_TIGER_HELPER_DIR="$DIR"
[ -f "$RES/faithful" ] && export TIGER_FAITHFUL=1
exec "$DIR/TigerBrowser2" "${TIGER_HOME_URL:-https://x.com/}" > /tmp/TigerBrowser.log 2>&1
'''


def assemble(root, env, payload):
    import plistlib
    binaries, resources, framework = payload
    destination = Path(env.get("BUNDLE", str(root / "build/TigerBrowser.app"))).resolve()
    token = uuid.uuid4().hex[:12]
    temporary = destination.with_name(destination.name + ".staging-" + token)
    previous = destination.with_name(destination.name + ".previous-" + token)
    try:
        for part in ("MacOS", "Resources", "Frameworks"):
            (temporary / "Contents" / part).mkdir(parents=True, exist_ok=True)
        for path in binaries:
            if path.name != "TigerWK2App":
                shutil.copy2(path, temporary / "Contents/MacOS" / path.name)
        for path in resources:
            shutil.copy2(path, temporary / "Contents/Resources" / path.name)
        shutil.copytree(framework, temporary / "Contents/Frameworks/QuartzCore.framework", symlinks=True)
        provenance(payload, temporary / "Contents/Resources/provenance")
        launcher = temporary / "Contents/MacOS/TigerBrowser"
        launcher.write_text(LAUNCHER)
        launcher.chmod(0o755)
        info = {"CFBundleDevelopmentRegion": "English", "CFBundleExecutable": "TigerBrowser",
                "CFBundleIdentifier": "org.webkittiger.TigerBrowser", "CFBundleInfoDictionaryVersion": "6.0",
                "CFBundleName": "TigerBrowser", "CFBundlePackageType": "APPL", "CFBundleSignature": "????",
                "CFBundleShortVersionString": "0.1", "CFBundleVersion": time.strftime("%Y%m%d.%H%M"),
                "LSMinimumSystemVersion": "10.4", "NSPrincipalClass": "NSApplication"}
        with (temporary / "Contents/Info.plist").open("wb") as target:
            plistlib.dump(info, target)
        (temporary / "Contents/PkgInfo").write_bytes(b"APPL????")
        if destination.exists():
            destination.rename(previous)
        try:
            temporary.rename(destination)
        except BaseException:
            if previous.exists() and not destination.exists():
                previous.rename(destination)
            raise
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)
    print("bundle: " + str(destination), flush=True)
    return destination


INSTALL_PROGRAM = r'''
set -eu
candidate=$1
destination=$2
previous=$3
if ps -axww -o command | grep -E '[/]Applications/TigerBrowser[.]app/|[/]TigerBrowser2([[:space:]]|$)' >/dev/null; then
    echo 'install: user app is open; staged bundle retained, nothing replaced' >&2
    exit 1
fi
test -d "$candidate"
test ! -e "$previous"
if test -e "$destination"; then mv "$destination" "$previous"; fi
if ! mv "$candidate" "$destination"; then
    if test -e "$previous"; then mv "$previous" "$destination"; fi
    exit 1
fi
'''


def bundle(root, env, remote, verifier=checked):
    with frozen_artifacts(root, env, verifier) as payload:
        destination = assemble(root, env, payload)
    if env.get("INSTALL", "1") == "0":
        return
    token = time.strftime("%Y%m%d-%H%M%S-") + uuid.uuid4().hex[:12]
    temporary = REMOTE_APPLICATIONS + "/.TigerBrowser.app.staging-" + token
    previous = REMOTE_APPLICATIONS + "/TigerBrowser.app.previous-" + token
    final = REMOTE_APPLICATIONS + "/TigerBrowser.app"
    timeout = int(env.get("LOCK_TIMEOUT", "600"))
    with local_lock(root, timeout), remote.lease(token, timeout):
        wait_idle(remote, int(env.get("BUSY_TIMEOUT", "300")))
        remote.run(["mkdir", "-p", temporary])
        remote.transfer([str(destination) + "/"], temporary + "/")
        remote.run(["sh", "-c", INSTALL_PROGRAM, "tiger-install", temporary, final, previous])
    print("installed: %s (previous retained at %s)" % (final, previous))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    run = commands.add_parser("stage")
    run.add_argument("url", nargs="?", default="http://example.com/")
    run.add_argument("seconds", nargs="?", type=int, default=15)
    commands.add_parser("bundle")
    args = parser.parse_args()
    env = dict(os.environ)
    remote = Remote(env.get("TIGER_HOST", "tiger-eth"))
    def interrupted(signum, frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, interrupted)
    try:
        if args.command == "stage":
            stage(ROOT, env, args.url, args.seconds, remote)
        else:
            bundle(ROOT, env, remote)
    except (RunError, ValueError, OSError, subprocess.CalledProcessError) as error:
        print("tiger-run: " + str(error), file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("tiger-run: interrupted; only this run's children were eligible for cleanup", file=sys.stderr)
        return 130
    return 0


if __name__ == "__main__":
    sys.exit(main())
