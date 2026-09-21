#!/usr/bin/env python3
"""Check that two configured WebKit trees agree on every IPC-relevant feature flag.

Why this exists
---------------
WebKit's serializer generator copies the conditionals out of the .serialization.in
and .messages.in files *verbatim* into the generated C++:

    # Source/WebKit/Scripts/generate-serializers.py:629 and nine other places
    result.append(f'#if {type.condition}')

So two processes sharing an IPC connection generate a byte-identical .cpp and then
compile *different* serializer sets out of it, according to each side's own feature
flags.  Nothing fails to build.  The wire simply desynchronises.

That also means hashing the generated sources cannot catch this: the sources are
identical by construction.  The only thing that knows the answer is the C
preprocessor, so this script runs it -- once per side, against that side's real
flags -- and diffs the results.  Preprocessing only: nothing is compiled, linked
or executed, which matters because we cross-compile i386 and x86_64 from an arm64
host and cannot run an i386 binary here at all.

It catches the 55-odd names that are compile-time macros in the PlatformEnable*.h
headers rather than CMake options, which is the class that matters most --
USE_CG and USE_CORE_TEXT among them -- and which wkcmake's CMake-level
tiger-ipc-features.txt records as "<unset>" because the build system never sees
them.

Usage
-----
    tools/check-wire-flags.py build/tiger-ui build/tiger-web
    tools/check-wire-flags.py --may-differ 'ENABLE(FOO)' build/a build/b
    tools/check-wire-flags.py --write-queue logs/wire-flag-queue.txt build/a build/b

Exit status is non-zero if any flag disagrees outside the allowlist.

See logs/serializer-asymmetry.md for the design and logs/n1-briefs.md N1-E.
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEBKIT = os.path.join(ROOT, "WebKit")

# A conditional token: ENABLE(X), USE(X), HAVE(X), PLATFORM(X), OS(X), CPU(X).
TOKEN = re.compile(r"\b(ENABLE|USE|HAVE|PLATFORM|OS|CPU)\(([A-Za-z0-9_]+)\)")

# Warning and diagnostic noise, dropped to keep the command line readable when it
# fails.  Note -O is deliberately NOT dropped: wtf/Compiler.h:125 #errors out on a
# release build without optimisation, so removing it breaks the preprocess.  The
# rule is to drop only what cannot change a conditional.
FLAG_DROP = re.compile(r"^(-W|-f(color|diagnostics)|--?Qunused)")

# Flags that take a separate following argument.
FLAG_TAKES_ARG = {"-isystem", "-cxx-isystem", "-include", "-I", "-D", "-U", "-mllvm"}


def find_generator_inputs(shared_list=None):
    """The .in files whose conditionals reach the wire.

    With --shared-inputs, use exactly that list (wkcmake's
    SHARED_SERIALIZATION_INPUTS, the intersection of the two source lists).
    Without it, fall back to every generator input under Source/WebKit, which is
    conservative: it can only report a flag that does not matter, never miss one
    that does.
    """
    if shared_list:
        with open(shared_list) as f:
            return [line.strip() for line in f if line.strip() and not line.startswith("#")]

    found = []
    for dirpath, _, filenames in os.walk(os.path.join(WEBKIT, "Source", "WebKit")):
        for name in filenames:
            if name.endswith((".serialization.in", ".messages.in")):
                found.append(os.path.join(dirpath, name))
    return sorted(found)


def extract_tokens(paths):
    """Every distinct conditional token appearing in an #if or #elif."""
    tokens = set()
    for path in paths:
        try:
            with open(path, encoding="utf-8", errors="replace") as f:
                for line in f:
                    stripped = line.lstrip()
                    if stripped.startswith("#if") or stripped.startswith("#elif"):
                        for kind, name in TOKEN.findall(stripped):
                            tokens.add(f"{kind}({name})")
        except OSError as exc:
            print(f"warning: cannot read {path}: {exc}", file=sys.stderr)
    return sorted(tokens)


def write_probe(tokens, path):
    """A translation unit that emits each token's evaluated value.

    The feature macros are defined as `(defined X && X)`, which only evaluates
    inside a preprocessor conditional, so each token needs its own #if/#else
    rather than being expanded in place.  The name is emitted as a string literal
    so it is not itself macro-expanded.
    """
    with open(path, "w") as f:
        f.write('#include "cmakeconfig.h"\n')
        f.write("#include <wtf/Platform.h>\n")
        for token in tokens:
            f.write(f"#if {token}\n__WIRE__ \"{token}\" 1\n")
            f.write(f"#else\n__WIRE__ \"{token}\" 0\n#endif\n")


def compiler_for(build_dir):
    """The C++ compiler this tree was configured with."""
    cmake_files = os.path.join(build_dir, "CMakeFiles")
    if os.path.isdir(cmake_files):
        for entry in sorted(os.listdir(cmake_files)):
            candidate = os.path.join(cmake_files, entry, "CMakeCXXCompiler.cmake")
            if os.path.exists(candidate):
                with open(candidate) as f:
                    match = re.search(r'set\(CMAKE_CXX_COMPILER "([^"]+)"', f.read())
                    if match:
                        return match.group(1)
    raise SystemExit(f"error: cannot find the C++ compiler for {build_dir}")


def preprocessor_flags(build_dir):
    """The real include and define flags this tree compiles with.

    Read out of build.ninja rather than reconstructed, so the probe sees exactly
    what a real translation unit sees.  Reconstructing them by hand is how a
    check like this quietly stops testing the thing it claims to test.
    """
    ninja = os.path.join(build_dir, "build.ninja")
    if not os.path.exists(ninja):
        raise SystemExit(f"error: {build_dir} is not configured (no build.ninja)")

    # Both DEFINES and FLAGS matter.  CMake's add_compile_definitions lands in
    # DEFINES, not FLAGS, and WTF_PLATFORM_TIGER is one of them -- reading only
    # FLAGS silently evaluated the whole probe as a non-Tiger build, which looked
    # like a real configuration bug until the missing -D was found.  Read both.
    raw = []
    seen_defines = seen_flags = False
    with open(ninja) as f:
        for line in f:
            if not seen_defines and line.startswith("  DEFINES = "):
                raw += line[len("  DEFINES = "):].strip().split()
                seen_defines = True
            elif not seen_flags and line.startswith("  FLAGS = "):
                raw += line[len("  FLAGS = "):].strip().split()
                seen_flags = True
            if seen_defines and seen_flags:
                break
    if not seen_flags:
        raise SystemExit(f"error: no FLAGS line in {ninja}")

    kept, index = [], 0
    while index < len(raw):
        flag = raw[index]
        if flag in FLAG_TAKES_ARG and index + 1 < len(raw):
            kept.extend([flag, raw[index + 1]])
            index += 2
            continue
        if not FLAG_DROP.match(flag):
            kept.append(flag)
        index += 1
    return kept


def evaluate(build_dir, probe_path, label):
    """Preprocess the probe against one tree and return {token: '0'|'1'}."""
    compiler = compiler_for(build_dir)
    command = [compiler, "-E", "-P", "-x", "c++"]
    command += preprocessor_flags(build_dir)
    command += ["-I", build_dir, "-I", os.path.join(WEBKIT, "Source", "WTF")]
    command += [probe_path]

    result = subprocess.run(command, capture_output=True, text=True)

    # A failed preprocess yields values that look plausible and are not, because
    # the headers that would have set them were never reached.  Refuse them.
    if result.returncode != 0:
        print(f"error: preprocessing failed for {label} ({build_dir})", file=sys.stderr)
        for line in result.stderr.splitlines():
            if "error" in line.lower():
                print(f"  {line}", file=sys.stderr)
        print(f"  command: {' '.join(command)}", file=sys.stderr)
        raise SystemExit(2)

    values = {}
    for line in result.stdout.splitlines():
        match = re.match(r'\s*__WIRE__\s+"([^"]+)"\s+([01])\s*$', line)
        if match:
            values[match.group(1)] = match.group(2)
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("build_a")
    parser.add_argument("build_b")
    parser.add_argument("--shared-inputs", metavar="FILE",
                        help="file listing the .in files compiled into both binaries "
                             "(wkcmake's SHARED_SERIALIZATION_INPUTS); defaults to all")
    parser.add_argument("--may-differ", action="append", default=[], metavar="TOKEN",
                        help="a flag allowed to differ; repeatable. Each use needs a "
                             "reason in the caller, and the list should stay near-empty")
    parser.add_argument("--write-queue", metavar="FILE",
                        help="write the disagreeing flags to FILE as a work queue")
    parser.add_argument("--note", metavar="TEXT", action="append", default=[],
                        help="a line of context to record in the written queue; repeatable")
    parser.add_argument("-q", "--quiet", action="store_true")
    args = parser.parse_args()

    inputs = find_generator_inputs(args.shared_inputs)
    tokens = extract_tokens(inputs)
    if not args.quiet:
        print(f"{len(inputs)} generator inputs, {len(tokens)} distinct conditionals")

    with tempfile.TemporaryDirectory() as tmp:
        probe = os.path.join(tmp, "WireFlagProbe.cpp")
        write_probe(tokens, probe)
        a = evaluate(args.build_a, probe, "A")
        b = evaluate(args.build_b, probe, "B")

    missing = [t for t in tokens if t not in a or t not in b]
    if missing:
        print(f"error: {len(missing)} token(s) produced no value, e.g. {missing[:3]}",
              file=sys.stderr)
        return 2

    allowed = set(args.may_differ)
    disagreements = [(t, a[t], b[t]) for t in tokens if a[t] != b[t]]
    unexpected = [d for d in disagreements if d[0] not in allowed]
    excused = [d for d in disagreements if d[0] in allowed]

    if not args.quiet:
        name_a = os.path.basename(os.path.normpath(args.build_a))
        name_b = os.path.basename(os.path.normpath(args.build_b))
        print(f"{len(tokens) - len(disagreements)} agree, "
              f"{len(unexpected)} disagree, {len(excused)} excused")
        for token, va, vb in unexpected:
            print(f"  DISAGREE {token}: {name_a}={va} {name_b}={vb}")
        for token, va, vb in excused:
            print(f"  excused  {token}: {name_a}={va} {name_b}={vb}")

    if args.write_queue:
        with open(args.write_queue, "w") as f:
            f.write(f"# Generated by tools/check-wire-flags.py\n")
            f.write(f"# {args.build_a} vs {args.build_b}\n")
            f.write(f"# {len(tokens)} conditionals from {len(inputs)} generator inputs\n#\n")
            for line in args.note:
                f.write(f"# {line}\n")
            if args.note:
                f.write("#\n")
            if not disagreements:
                f.write("# No disagreements. The two trees agree on every\n")
                f.write("# IPC-relevant conditional.\n")
            for token, va, vb in disagreements:
                tag = "excused" if token in allowed else "DISAGREE"
                f.write(f"{tag} {token} {os.path.basename(os.path.normpath(args.build_a))}={va}"
                        f" {os.path.basename(os.path.normpath(args.build_b))}={vb}\n")
        if not args.quiet:
            print(f"wrote {args.write_queue}")

    return 1 if unexpected else 0


if __name__ == "__main__":
    sys.exit(main())
