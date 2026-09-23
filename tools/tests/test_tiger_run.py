"""Local tests only: no SSH, install, or Tiger workload is started."""
import contextlib
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location("tiger_run", Path(__file__).resolve().parents[1] / "tiger-run.py")
RUN = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUN)


class FakeRemote:
    def __init__(self, busy="", fail_transfer=False, fail_run=False):
        self.calls = []
        self.busy = busy
        self.fail_transfer = fail_transfer
        self.fail_run = fail_run

    @contextlib.contextmanager
    def lease(self, token, timeout):
        self.calls.append(("lease", token))
        try:
            yield
        finally:
            self.calls.append(("release", token))

    def run(self, argv, **kwargs):
        self.calls.append(("run", [str(arg) for arg in argv]))
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
        perl = [call[1] for call in remote.calls if call[0] == "run" and call[1][0] == "perl"]
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


if __name__ == "__main__":
    unittest.main()
