# Tiger realpath compatibility regression

Tiger's native `realpath` requires a caller-supplied buffer. Modern libc++ calls
`realpath(path, NULL)` to allocate one. The compatibility wrapper must be linked
into both the 32-bit UI/test runner and the 64-bit content process. A 64-bit-only
guard caused WebKitTestRunner to crash in native `realpath` before loading a test.
Tiger also permits a missing final component, as documented in Apple's
[legacy realpath description](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man3/realpath.3.html).
The wrapper uses native canonicalization and public `stat` to require that the
original path exists, including directory traversal and trailing-slash semantics.

Rebuild the installed archives, then run the probe:

```sh
make -C compat ARCH=i386 -j2 install
make -C compat ARCH=x86_64 -j2 install
python3 spike/realpath/run-probe.py
```

The script uses `TIGER_HOST` (default `tiger-eth`), compiles one executable for each
ABI, takes the regular local and remote Tiger leases, and runs each with a 20-second
remote alarm. It uses and removes unique private temporary directories. Results
and local SHA256 identities are retained in `logs/realpath/<run>/`; the script
exits nonzero if either native probe fails. It does not build or launch WebKit.

Each probe checks allocated relative, absolute, normalized and symbolic-link
paths; supplied-buffer identity and normalization; empty/missing paths; dangling
and looping symlinks; and non-directory traversal/trailing-slash failures for both
calling conventions. Passing establishes these filesystem semantics on the
device, not successful WebKitTestRunner startup: rebuild all matching artifacts
after changing their shared dependency identity, then run the runner smoke tests.
