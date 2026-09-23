"""Mocked Mini toolchain/SSH tests; never contact either physical Mac."""
import contextlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time
import unittest
from unittest import mock


SPEC = importlib.util.spec_from_file_location("mini_build", Path(__file__).resolve().parents[2] / "tools/mini-build.py")
mini = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(mini)


def executable(path, source):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(source)
    path.chmod(0o755)


class MiniBuildTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="mini build test ")
        self.root = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)
        (self.root / "toolchain").mkdir()
        (self.root / "toolchain/tiger64.cmake").write_text("toolchain")
        (self.root / "WebKit-test").mkdir()
        (self.root / "build").mkdir()
        (self.root / "build/web-opts.txt").write_text("-DCMAKE_BUILD_TYPE:STRING=Release\n")
        self.build = self.root / "build/tiger-web-test"
        executable(self.root / "cmake/bin/cmake", """#!/usr/bin/env python3
import os, sys
from pathlib import Path
b=Path(sys.argv[sys.argv.index('-B')+1]); b.mkdir(parents=True, exist_ok=True)
with (b/'configured').open('a') as f: f.write('configure\\n')
print('configure full output')
if os.environ.get('MOCK_CMAKE_FAIL'): sys.exit(12)
source=sys.argv[sys.argv.index('-S')+1]
flags=dict(a[2:].split('=',1) for a in sys.argv if a.startswith('-D'))
(b/'CMakeCache.txt').write_text('CMAKE_HOME_DIRECTORY:INTERNAL='+source+'\\nTIGER_PROCESS:STRING='+flags['TIGER_PROCESS:STRING']+'\\nCMAKE_TOOLCHAIN_FILE:FILEPATH='+flags['CMAKE_TOOLCHAIN_FILE:FILEPATH']+'\\n')
""")
        executable(self.root / "bin/ninja", """#!/usr/bin/env python3
import os,sys
from pathlib import Path
b=Path(sys.argv[sys.argv.index('-C')+1]); (b/'bin').mkdir(exist_ok=True)
for i in range(100): print('full ninja output %d'%i)
if os.environ.get('MOCK_NINJA_FAIL'): sys.exit(17)
for name in sys.argv[4:]:
 p=b/'bin'/name; p.write_text('fresh '+name); p.chmod(0o755)
""")
        executable(self.root / "tools/tiger-artifacts.py", """#!/usr/bin/env python3
import hashlib,json,sys
from pathlib import Path
p=Path(sys.argv[sys.argv.index('--out')+1]); p.mkdir(exist_ok=True)
result={}
for kind in ('messages','serializers'):
 f=p/('wire-'+kind+'.txt'); f.write_text(kind+'\\n')
 result[kind+'_sha256']=hashlib.sha256(f.read_bytes()).hexdigest()
print('diagnostic on stderr',file=sys.stderr)
print(json.dumps(result))
""")
        paths = ["toolchain/tiger64.cmake"]
        self.job = {"root": str(self.root), "build": "tiger-web-test", "worktree": "WebKit-test",
                    "process": "WEB", "toolchain": "tiger64.cmake", "options": [], "extra_cmake": [], "jobs": 2,
                    "targets": ["TigerWebProcess", "TigerNetworkProcess"], "source": {"head": "a" * 40, "tree": "b" * 40, "dirty_sha256": mini.json_hash("")},
                    "outer": {}, "input_paths": paths, "inputs": mini.inventory(self.root, paths), "id": "test-job"}

    def test_test_helper_target_requires_the_injected_output(self):
        executable(self.build / "bin/TigerWebProcess", "production")
        with self.assertRaisesRegex(mini.BuildError, "found 0"):
            mini.artifact_paths(self.build, ["TigerWebProcessTests"])
        injected = self.build / "bin/wktr/TigerWebProcess"
        executable(injected, "injected")
        self.assertEqual(mini.artifact_paths(self.build, ["TigerWebProcessTests"]), [injected])

    def test_failed_ninja_cannot_certify_stale_binary_and_keeps_full_log(self):
        executable(self.build / "bin/TigerWebProcess", "stale")
        (self.build / "build-manifest.json").write_text('{"old":true}')
        with mock.patch.dict(os.environ, {"MOCK_NINJA_FAIL": "1"}):
            with self.assertRaisesRegex(mini.BuildError, "17"):
                mini.remote_build(self.job)
        self.assertFalse((self.build / "build-manifest.json").exists())
        self.assertEqual((self.build / "bin/TigerWebProcess").read_text(), "stale")
        log = (self.build / "mini-ninja.log").read_text()
        self.assertIn("full ninja output 0\n", log)
        self.assertIn("full ninja output 99\n", log)

    def test_configuration_failure_never_runs_ninja(self):
        with mock.patch.dict(os.environ, {"MOCK_CMAKE_FAIL": "1"}):
            with self.assertRaisesRegex(mini.BuildError, "12"):
                mini.remote_build(self.job)
        self.assertFalse((self.build / "mini-ninja.log").exists())
        self.assertFalse((self.build / "build-manifest.json").exists())

    def test_existing_configuration_is_reconfigured_and_only_requested_outputs_certified(self):
        executable(self.build / "bin/OldOtherExecutable", "unrelated")
        mini.remote_build(self.job)
        mini.remote_build(self.job)
        manifest = json.loads((self.build / "build-manifest.json").read_text())
        self.assertEqual(set(manifest["binaries"]), set(self.job["targets"]))
        self.assertEqual(len((self.build / "configured").read_text().splitlines()), 2)
        self.assertEqual(manifest["wire"]["messages_sha256"], mini.digest(self.build / "wire-messages.txt"))
        self.assertIn("diagnostic on stderr", (self.build / "mini-wire.log").read_text())

    def test_wrong_source_process_or_toolchain_refused_before_configure(self):
        cases = {"CMAKE_HOME_DIRECTORY:INTERNAL": "/another/source", "TIGER_PROCESS:STRING": "GPU", "CMAKE_TOOLCHAIN_FILE:FILEPATH": "/wrong/toolchain"}
        self.build.mkdir()
        for key, value in cases.items():
            with self.subTest(key=key):
                (self.build / "CMakeCache.txt").write_text(key + "=" + value + "\n")
                with self.assertRaisesRegex(mini.BuildError, "identity mismatch"):
                    mini.remote_build(self.job)
                self.assertFalse((self.build / "configured").exists())

    def test_dependency_mismatch_refused_before_configure(self):
        (self.root / "toolchain/tiger64.cmake").write_text("changed")
        with self.assertRaisesRegex(mini.BuildError, "content differs"):
            mini.remote_build(self.job)
        self.assertFalse((self.build / "configured").exists())

    def test_download_corruption_does_not_replace_previous_local_artifact(self):
        mini.remote_build(self.job)
        destination = self.root / "published"
        executable(destination / "bin/TigerWebProcess", "previous")
        (self.build / "bin/TigerWebProcess").write_text("corrupt download")
        with self.assertRaisesRegex(mini.BuildError, "hash/path mismatch"):
            mini.publish(self.build, destination)
        self.assertEqual((destination / "bin/TigerWebProcess").read_text(), "previous")

    def test_failed_remote_build_never_downloads_or_publishes(self):
        @contextlib.contextmanager
        def lease(*args):
            yield mock.Mock(poll=lambda: None)
        calls = []
        def ssh(host, args):
            if "--remote" in args:
                raise mini.BuildError("remote ninja failed")
        with mock.patch.dict(os.environ, {"WKT": str(self.root)}), \
             mock.patch.object(mini, "mini_lock", lease), \
             mock.patch.object(mini, "input_paths", return_value=self.job["input_paths"]), \
             mock.patch.object(mini, "git_identity", return_value=self.job["source"]), \
             mock.patch.object(mini, "sync_path"), mock.patch.object(mini, "ssh", ssh), \
             mock.patch.object(mini, "run", side_effect=lambda args: calls.append(args)), \
             mock.patch.object(mini.subprocess, "run"), mock.patch.object(mini, "publish") as publish:
            with self.assertRaisesRegex(mini.BuildError, "remote ninja failed"):
                mini.main(["WebKit-test", "tiger-web-test", "TigerWebProcess"])
        self.assertFalse(publish.called)
        self.assertEqual(len(calls), 1)  # job upload only; never fetch old bin/ or manifest

    def test_remote_lease_excludes_other_callers_and_cleans_up(self):
        executable(self.root / "fake-bin/ssh", '#!/bin/sh\nexec /bin/sh -c "$2"\n')
        executable(self.root / "fake-bin/pgrep", '#!/bin/sh\nexit 1\n')
        executable(self.root / "fake-bin/sysctl", '#!/bin/sh\necho "{ 0.00 0.00 0.00 }"\n')
        acquired = threading.Event()
        errors = []
        def contender():
            try:
                with mini.mini_lock("fake-host", self.root, 10):
                    acquired.set()
            except Exception as error:
                errors.append(error)
        with mock.patch.dict(os.environ, {"PATH": str(self.root / "fake-bin") + ":" + os.environ["PATH"]}):
            with mini.mini_lock("fake-host", self.root, 10):
                thread = threading.Thread(target=contender)
                thread.start()
                self.assertFalse(acquired.wait(0.2))
            thread.join(10)
        self.assertFalse(thread.is_alive())
        self.assertFalse(errors)
        self.assertTrue(acquired.is_set())
        self.assertFalse((self.root / "build/.mini-build.lock").exists())

    def test_legacy_ninja_must_finish_before_lease_is_ready(self):
        executable(self.root / "fake-bin/ssh", '#!/bin/sh\nexec /bin/sh -c "$2"\n')
        executable(self.root / "fake-bin/sysctl", '#!/bin/sh\necho "{ 0.00 0.00 0.00 }"\n')
        # Marker removal models an already-running, unmanaged Ninja exiting.
        marker = self.root / "legacy-ninja-running"
        marker.touch()
        executable(self.root / "fake-bin/pgrep", '#!/bin/sh\ntest -f "$MOCK_NINJA_MARKER"\n')
        executable(self.root / "fake-bin/sleep", '#!/bin/sh\nexec /bin/sleep 0.1\n')
        acquired = threading.Event()
        errors = []
        def waiter():
            try:
                with mini.mini_lock("fake-host", self.root, 10):
                    acquired.set()
            except Exception as error:
                errors.append(error)
        with mock.patch.dict(os.environ, {"PATH": str(self.root / "fake-bin") + ":" + os.environ["PATH"], "MOCK_NINJA_MARKER": str(marker)}):
            thread = threading.Thread(target=waiter)
            thread.start()
            self.assertFalse(acquired.wait(0.25))
            marker.unlink()
            thread.join(5)
        self.assertFalse(thread.is_alive())
        self.assertFalse(errors)
        self.assertTrue(acquired.is_set())


if __name__ == "__main__":
    unittest.main()
