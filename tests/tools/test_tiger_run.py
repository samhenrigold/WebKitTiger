"""Local tests only: no SSH, install, or Tiger workload is started."""
import contextlib
import importlib.util
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location("tiger_run", Path(__file__).resolve().parents[2] / "tools/tiger-run.py")
RUN = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUN)


class FakeRemote:
    def __init__(self, busy="", fail_transfer=False, fail_run=False):
        self.calls = []
        self.busy = busy
        self.fail_transfer = fail_transfer
        self.fail_run = fail_run
        self.cached_keys = set()

    @contextlib.contextmanager
    def lease(self, token, timeout):
        self.calls.append(("lease", token))
        try:
            yield
        finally:
            self.calls.append(("release", token))

    def run(self, argv, **kwargs):
        self.calls.append(("run", [str(arg) for arg in argv]))
        if argv[:3] == ["perl", "-e", RUN.BINARY_CACHE_PROGRAM]:
            action, key = argv[3], argv[5]
            if action == "publish":
                self.cached_keys.add(key)
            output = ("HIT\n" if key in self.cached_keys else "MISS\n") if action == "use" else ""
            return subprocess.CompletedProcess(argv, 0, output, "")
        if len(argv) > 2 and argv[0] == "perl" and argv[2] == "run" and self.fail_run:
            raise subprocess.CalledProcessError(1, argv)
        return subprocess.CompletedProcess(argv, 0, self.busy if argv[0] == "ps" else "", "")

    def transfer(self, sources, destination):
        self.calls.append(("transfer", destination))
        if self.fail_transfer:
            raise subprocess.CalledProcessError(1, ["rsync"])

    def receive(self, source, destination):
        self.calls.append(("receive", source))
        Path(destination).parent.mkdir(parents=True, exist_ok=True)
        Path(destination).write_text("test app log\n")


class TigerRunTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        paths = ["build/tiger-ui-port/bin/TigerWK2App", "build/tiger-ui-port/bin/TigerBrowser2",
                 "build/tiger-web-port/bin/TigerWebProcess", "build/tiger-web-port/bin/TigerNetworkProcess",
                 "build/tiger-gpu/bin/TigerGPUProcess", "build/tigeraudio32", "logs/tiger-fonts.json",
                 "deps/src/cacert.pem", "spike/wk2web/example.html", "tools/tiger-run-remote.pl"]
        for name in paths:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("fixture")
            path.chmod(0o755)
        (self.root / "spike/CAHost/Frameworks/QuartzCore.framework").mkdir(parents=True)
        (self.root / "spike/CAHost/Frameworks/QuartzCore.framework/QuartzCore").write_text("framework")
        for directory in ("tiger-ui-port", "tiger-web-port", "tiger-gpu"):
            for name in ("build-manifest.json", "wire-messages.txt", "wire-serializers.txt"):
                (self.root / "build" / directory / name).write_text("fixture")
        self.verifier = mock.Mock()

    def test_missing_helper_fails_before_any_remote_operation(self):
        (self.root / "build/tiger-gpu/bin/TigerGPUProcess").unlink()
        remote = FakeRemote()
        with self.assertRaisesRegex(RUN.RunError, "missing required"):
            RUN.stage(self.root, {}, "https://example.com", 15, remote, self.verifier)
        self.assertEqual(remote.calls, [])
        self.verifier.assert_not_called()

    def test_wire_verification_failure_precedes_remote_operations(self):
        self.verifier.side_effect = subprocess.CalledProcessError(1, ["verify"])
        remote = FakeRemote()
        with self.assertRaises(subprocess.CalledProcessError):
            RUN.stage(self.root, {}, "https://example.com", 15, remote, self.verifier)
        self.assertEqual(remote.calls, [])

    def test_missing_framework_binary_fails_before_remote_operations(self):
        (self.root / "spike/CAHost/Frameworks/QuartzCore.framework/QuartzCore").unlink()
        remote = FakeRemote()
        with self.assertRaisesRegex(RUN.RunError, "missing required framework binary"):
            RUN.stage(self.root, {}, "https://example.com", 15, remote, self.verifier)
        self.assertEqual(remote.calls, [])

    def test_snapshot_is_checked_before_remote_operations(self):
        self.verifier.side_effect = [None, subprocess.CalledProcessError(1, ["snapshot-verify"])]
        remote = FakeRemote()
        with self.assertRaises(subprocess.CalledProcessError):
            RUN.stage(self.root, {}, "https://example.com", 15, remote, self.verifier)
        self.assertEqual(remote.calls, [])
        self.assertEqual(self.verifier.call_count, 2)

    def test_transfer_failure_never_launches(self):
        remote = FakeRemote(fail_transfer=True)
        with self.assertRaises(subprocess.CalledProcessError):
            RUN.stage(self.root, {}, "https://example.com", 15, remote, self.verifier)
        self.assertFalse(any(call[0] == "run" and call[1][0] == "perl" for call in remote.calls))
        self.assertEqual(remote.calls[-1][0], "release")

    def test_cache_can_be_disabled_for_direct_per_run_copies(self):
        remote = FakeRemote()
        RUN.stage(self.root, {"BINARY_CACHE": "0"}, "https://example.com", 1, remote, self.verifier)
        self.assertTrue(any(call[0] == "transfer" and call[1].endswith("/bin/") for call in remote.calls))
        self.assertFalse(any(call[0] == "run" and RUN.BINARY_CACHE_PROGRAM in call[1] for call in remote.calls))

    def test_active_user_app_not_transferred_or_killed(self):
        remote = FakeRemote(busy="123 /Users/shg/Applications/TigerBrowser.app/Contents/MacOS/TigerBrowser2\n")
        with self.assertRaisesRegex(RUN.RunError, "box still busy"):
            RUN.stage(self.root, {"BUSY_TIMEOUT": "0"}, "https://example.com", 15, remote, self.verifier)
        self.assertFalse(any(call[0] == "transfer" for call in remote.calls))
        commands = [call[1][0] for call in remote.calls if call[0] == "run"]
        self.assertEqual(commands, ["ps"])

    def test_cleanup_is_bound_to_failed_run_only(self):
        remote = FakeRemote(fail_run=True)
        with self.assertRaises(subprocess.CalledProcessError):
            RUN.stage(self.root, {}, "https://example.com", 15, remote, self.verifier)
        perl = [call[1] for call in remote.calls if call[0] == "run" and call[1][0] == "perl"
                and call[1][2] in ("run", "cleanup")]
        self.assertEqual([call[2] for call in perl], ["run", "cleanup"])
        self.assertEqual(perl[0][3], perl[1][3])
        self.assertTrue(perl[0][3].startswith(RUN.REMOTE_BASE + "/runs/"))
        self.assertFalse(any("killall" in str(call) or "rm -f /tmp" in str(call) for call in remote.calls))
        self.assertTrue(any(call[0] == "receive" and call[1].endswith("/app.log") for call in remote.calls))

    def test_runs_get_distinct_paths_and_default_outputs_are_untracked(self):
        remote = FakeRemote()
        RUN.stage(self.root, {}, "https://example.com", 1, remote, self.verifier)
        RUN.stage(self.root, {}, "https://example.com", 1, remote, self.verifier)
        runs = [call[1][3] for call in remote.calls if call[0] == "run" and call[1][0] == "perl" and call[1][2] == "run"]
        self.assertEqual(len(set(runs)), 2)
        self.assertEqual(len(list((self.root / "build/runs").glob("*/shot.png"))), 2)
        self.assertFalse((self.root / "spike/wk2web/first-window.png").exists())

    def test_quoted_values_and_wrapper_are_argv_not_shell(self):
        script = "wait 1; type $(touch /tmp/never); type `whoami`; type 'quoted'"
        value = "TIGER_SCRIPT=" + shlex.quote(script) + " FLAG=yes /Users/shg/wk2/share/benchstamp.pl"
        assignments, wrapper = RUN.parse_app_env(value)
        self.assertEqual(assignments, {"TIGER_SCRIPT": script, "FLAG": "yes"})
        self.assertEqual(wrapper, ["/Users/shg/wk2/share/benchstamp.pl"])
        remote = RUN.Remote("tiger-eth")
        argv = ["env", "TIGER_SCRIPT=" + script, "/path with spaces/app", "https://x/?q=';echo pwn"]
        self.assertEqual(shlex.split(remote.command(argv)[-1]), argv)

    def test_home_is_isolated_and_cookie_profile_persists(self):
        path = RUN.REMOTE_BASE + "/runs/test"
        self.assertEqual(RUN.run_environment({}, path)[0]["HOME"], RUN.REMOTE_BASE + "/home")
        self.assertEqual(RUN.run_environment({"RUN_HOME_NEW": "1"}, path)[0]["HOME"], path + "/home")
        env = {"APP_ENV": "HOME=/Users/shg/wk2/cookiehome WEBKIT_TIGER_HELPER_DIR=/wrong"}
        values, _ = RUN.run_environment(env, path)
        self.assertEqual(values["HOME"], RUN.REMOTE_BASE + "/cookiehome")
        self.assertEqual(values["WEBKIT_TIGER_HELPER_DIR"], path + "/bin")
        for home in ("/Users/shg", "/Users/shg/wk2/../../shg", "/Users/shg/wk2"):
            with self.assertRaises(RUN.RunError):
                RUN.run_environment({"APP_ENV": "HOME=" + home}, path)

    def test_bundle_only_retains_previous_and_never_connects(self):
        destination = self.root / "build/TigerBrowser.app"
        destination.mkdir()
        (destination / "old-marker").write_text("old")
        remote = FakeRemote()
        RUN.bundle(self.root, {"INSTALL": "0"}, remote, self.verifier)
        self.assertEqual(remote.calls, [])
        self.assertTrue((destination / "Contents/MacOS/TigerBrowser2").exists())
        self.assertEqual(len(list(destination.parent.glob("TigerBrowser.app.previous-*/old-marker"))), 1)

    def test_failed_install_transfer_never_promotes(self):
        remote = FakeRemote(fail_transfer=True)
        with self.assertRaises(subprocess.CalledProcessError):
            RUN.bundle(self.root, {}, remote, self.verifier)
        self.assertFalse(any(call[0] == "run" and call[1][0] == "sh" for call in remote.calls))
        self.assertTrue(any(call[0] == "transfer" and ".staging-" in call[1] for call in remote.calls))

    def test_install_refuses_active_user_app(self):
        remote = FakeRemote(busy="123 /Users/shg/Applications/TigerBrowser.app/Contents/MacOS/TigerBrowser2\n")
        with self.assertRaises(RUN.RunError):
            RUN.bundle(self.root, {"BUSY_TIMEOUT": "0"}, remote, self.verifier)
        self.assertFalse(any(call[0] == "transfer" for call in remote.calls))

    def test_custom_build_directories_are_used(self):
        env = {}
        for variable, old in (("UIDIR", "tiger-ui-port"), ("WEBDIR", "tiger-web-port"), ("GPUDIR", "tiger-gpu")):
            destination = self.root / ("custom " + variable)
            (self.root / "build" / old).rename(destination)
            env[variable] = str(destination)
        remote = FakeRemote()
        RUN.bundle(self.root, dict(env, INSTALL="0"), remote, self.verifier)
        original_args = [str(arg) for arg in self.verifier.call_args_list[0].args[0]]
        self.assertIn(str(Path(env["UIDIR"]).resolve()), original_args)
        self.assertIn(str(Path(env["WEBDIR"]).resolve()), original_args)
        self.assertIn(str(Path(env["GPUDIR"]).resolve()), original_args)

    def test_perl_lease_releases_when_owner_disconnects(self):
        lock = str(self.root / "remote-lease")
        owner = subprocess.Popen(["perl", "-e", RUN.LEASE_PROGRAM, lock, "0", "owner"],
                                 stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            self.assertEqual(owner.stdout.readline().strip(), "TIGER-LEASE owner")
            contender = subprocess.run(["perl", "-e", RUN.LEASE_PROGRAM, lock, "0", "other"],
                                       input="", capture_output=True, text=True, timeout=5)
            self.assertNotEqual(contender.returncode, 0)
            self.assertIn("box busy", contender.stderr)
            owner.stdin.close()
            self.assertEqual(owner.wait(timeout=5), 0)
            successor = subprocess.run(["perl", "-e", RUN.LEASE_PROGRAM, lock, "0", "successor"],
                                       input="", capture_output=True, text=True, timeout=5)
            self.assertEqual(successor.returncode, 0)
            self.assertIn("TIGER-LEASE successor", successor.stdout)
        finally:
            if owner.poll() is None:
                owner.terminate()
                owner.wait(timeout=5)
            if not owner.stdin.closed:
                owner.stdin.close()
            owner.stdout.close()
            owner.stderr.close()

    def install_fixture(self, busy=False, fail_promotion=False):
        directory = self.root / "applications"
        directory.mkdir()
        candidate = directory / ".TigerBrowser.app.staging-test"
        destination = directory / "TigerBrowser.app"
        previous = directory / "TigerBrowser.app.previous-test"
        candidate.mkdir()
        destination.mkdir()
        (candidate / "marker").write_text("new")
        (destination / "marker").write_text("old")
        commands = self.root / "commands"
        commands.mkdir()
        ps = commands / "ps"
        ps.write_text("#!/bin/sh\n" + ("echo /Users/shg/Applications/TigerBrowser.app/Contents/MacOS/TigerBrowser2\n" if busy else "exit 0\n"))
        ps.chmod(0o755)
        if fail_promotion:
            mv = commands / "mv"
            mv.write_text('#!/bin/sh\ncase "$1" in *.staging-*) exit 1;; esac\nexec /bin/mv "$@"\n')
            mv.chmod(0o755)
        env = dict(os.environ, PATH=str(commands) + os.pathsep + os.environ["PATH"])
        result = subprocess.run(["sh", "-c", RUN.INSTALL_PROGRAM, "test-install", str(candidate), str(destination), str(previous)],
                                env=env, capture_output=True, text=True)
        return result, candidate, destination, previous

    def test_atomic_promotion_retains_previous_bundle(self):
        result, candidate, destination, previous = self.install_fixture()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((destination / "marker").read_text(), "new")
        self.assertEqual((previous / "marker").read_text(), "old")
        self.assertFalse(candidate.exists())

    def test_user_opening_during_transfer_prevents_promotion(self):
        result, candidate, destination, previous = self.install_fixture(busy=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("nothing replaced", result.stderr)
        self.assertEqual((destination / "marker").read_text(), "old")
        self.assertTrue(candidate.exists())
        self.assertFalse(previous.exists())

    def test_failed_final_rename_restores_previous_bundle(self):
        result, candidate, destination, previous = self.install_fixture(fail_promotion=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual((destination / "marker").read_text(), "old")
        self.assertEqual((candidate / "marker").read_text(), "new")
        self.assertFalse(previous.exists())


class LocalCacheRemote:
    """Run the actual Tiger-compatible cache program against temporary local files."""
    def __init__(self, fail_transfer=False, corrupt_transfer=False):
        self.transfers = 0
        self.fail_transfer = fail_transfer
        self.corrupt_transfer = corrupt_transfer

    def run(self, argv, **kwargs):
        if argv[:3] != ["perl", "-e", RUN.BINARY_CACHE_PROGRAM]:
            raise AssertionError("unexpected command: " + str(argv))
        kwargs.setdefault("capture_output", True)
        kwargs.setdefault("text", True)
        return subprocess.run(argv, check=True, timeout=10, **kwargs)

    def transfer(self, sources, destination):
        self.transfers += 1
        for source in sources:
            shutil.copy2(source, Path(destination) / source.name)
            if self.fail_transfer:
                raise subprocess.CalledProcessError(1, ["mock-rsync"])
        if self.corrupt_transfer:
            path = Path(destination) / sources[0].name
            path.write_bytes(b"X" * path.stat().st_size)


class BinaryCacheTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix="tiger-binary-cache-test-")
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.remote_base = self.root / "remote"
        self.remote_base.mkdir()
        patch = mock.patch.object(RUN, "REMOTE_BASE", str(self.remote_base))
        patch.start()
        self.addCleanup(patch.stop)
        # Cache files/directories are intentionally read-only; permit test teardown.
        self.addCleanup(self.make_writable)
        self.binaries = []
        for name in RUN.APPS + ("TigerWebProcess", "TigerNetworkProcess", "TigerGPUProcess", "tigeraudio32"):
            path = self.root / name
            path.write_bytes(("frozen " + name).encode())
            path.chmod(0o755)
            self.binaries.append(path)
        self.remote = LocalCacheRemote()

    def make_writable(self):
        for path in self.root.rglob("*"):
            if path.is_dir():
                path.chmod(0o700)

    def destination(self, name):
        path = self.remote_base / "runs" / name / "bin"
        path.mkdir(parents=True)
        return path

    def populate(self, name="first"):
        destination = self.destination(name)
        RUN.cached_binaries(self.remote, self.binaries, str(destination))
        key, manifest = RUN.binary_cache_identity(self.binaries)
        return destination, self.remote_base / ".binary-cache-v1" / key, manifest

    def test_cache_hit_uses_verified_read_only_hardlinks_without_transfer(self):
        first, pool, manifest = self.populate()
        self.assertEqual((pool / "COMPLETE").read_text(), manifest)
        self.assertEqual(pool.stat().st_mode & 0o7777, 0o555)
        second, second_pool, _ = self.populate("second")
        self.assertEqual(second_pool, pool)
        self.assertEqual(self.remote.transfers, 1)
        for source in self.binaries:
            cached = pool / source.name
            self.assertEqual(cached.read_bytes(), source.read_bytes())
            self.assertEqual(cached.stat().st_mode & 0o7777, 0o555)
            self.assertTrue(cached.samefile(first / source.name))
            self.assertTrue(cached.samefile(second / source.name))

    def test_changing_any_frozen_binary_including_audio_selects_a_new_pool(self):
        first, old_pool, _ = self.populate()
        original = {path.name: path.read_bytes() for path in self.binaries}
        previous_key = old_pool.name
        for index, source in enumerate(self.binaries):
            with self.subTest(binary=source.name):
                source.write_bytes(source.read_bytes() + b" changed")
                destination, pool, _ = self.populate("change-" + str(index))
                self.assertNotEqual(pool.name, previous_key)
                self.assertEqual((destination / source.name).read_bytes(), source.read_bytes())
                self.assertEqual((first / source.name).read_bytes(), original[source.name])
                previous_key = pool.name
        self.assertEqual(self.remote.transfers, 7)

    def test_failed_population_is_never_published_or_linked(self):
        self.remote.fail_transfer = True
        destination = self.destination("failed")
        with self.assertRaises(subprocess.CalledProcessError):
            RUN.cached_binaries(self.remote, self.binaries, str(destination))
        self.assertEqual(list(destination.iterdir()), [])
        self.assertEqual(list((self.remote_base / ".binary-cache-v1").iterdir()), [])

    def test_corrupt_upload_is_rejected_before_complete_marker_publication(self):
        self.remote.corrupt_transfer = True
        destination = self.destination("corrupt-upload")
        with self.assertRaises(subprocess.CalledProcessError):
            RUN.cached_binaries(self.remote, self.binaries, str(destination))
        self.assertEqual(list(destination.iterdir()), [])
        self.assertEqual(list((self.remote_base / ".binary-cache-v1").iterdir()), [])

    def test_marker_does_not_override_corrupt_or_writable_cached_content(self):
        first, pool, _ = self.populate()
        binary = pool / self.binaries[0].name
        old_time = binary.stat().st_mtime_ns
        original = binary.read_bytes()
        for mode in ("writable", "corrupt"):
            with self.subTest(mode=mode):
                binary.chmod(0o755)
                if mode == "corrupt":
                    binary.write_bytes(b"X" * len(original))
                    os.utime(binary, ns=(old_time, old_time))
                    binary.chmod(0o555)
                destination = self.destination(mode)
                with self.assertRaises(subprocess.CalledProcessError):
                    RUN.cached_binaries(self.remote, self.binaries, str(destination))
                self.assertEqual(list(destination.iterdir()), [])
                self.assertEqual(self.remote.transfers, 1)
                binary.chmod(0o555)


if __name__ == "__main__":
    unittest.main()
