// x86_64 C++ exception coverage for Tiger. Unwinding on x86_64 reaches libunwind through
// _dyld_find_unwind_sections (compat/libcompat.c), so this is the test that fails loudly if
// that shim is wrong or if a stale libtigercompat.a is on the link line.
// Build/run: spike/run64.sh spike/cxx64exc.cpp
#include <cstdio>
#include <stdexcept>
#include <string>
#include "throwlib.h"

static int failures = 0;
static void check(const char *what, bool ok)
{
    printf("%-52s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

struct Guard {
    bool *ran;
    explicit Guard(bool *r) : ran(r) {}
    ~Guard() { *ran = true; }
};

static void throwRuntimeError() { throw std::runtime_error("boom"); }
static void deepThrow(int depth) { if (depth == 0) throw std::runtime_error("deep"); deepThrow(depth - 1); }
static void throwLogicError() { throw std::logic_error("logic"); }

int main()
{
    bool caught = false;
    try { throwRuntimeError(); } catch (const std::runtime_error &e) { caught = std::string(e.what()) == "boom"; }
    check("std::runtime_error caught by reference", caught);

    caught = false;
    try { throw 42; } catch (int v) { caught = (v == 42); }
    check("catch by value (int)", caught);

    // Eight frames of unwinding, which needs the FDE lookup to be right, not just the first frame.
    caught = false;
    try { deepThrow(8); } catch (const std::runtime_error &e) { caught = std::string(e.what()) == "deep"; }
    check("throw unwound through 8 frames", caught);

    bool ranDtor = false;
    try { Guard g(&ranDtor); throwRuntimeError(); } catch (const std::exception &) {}
    check("destructor ran during unwinding", ranDtor);

    // A type defined in another translation unit, thrown there and caught here: this is the case
    // that fails when typeinfo is not coalesced across the static-library boundary.
    caught = false;
    try { throwFromLib(1); } catch (const LibError &e) { caught = std::string(e.what()) == "thrown in dylib"; }
    check("custom class thrown in a static lib, caught here", caught);

    // Same boundary in the other direction: thrown here, caught there, matched by base class.
    check("thrown here, caught in the static lib by base class", catchInLib(throwLogicError) == 1);

    // A type from the static lib must still match its std:: base in this image.
    caught = false;
    try { throwFromLib(1); } catch (const std::runtime_error &) { caught = true; }
    check("static-lib exception matches its std:: base here", caught);

    caught = false;
    try { try { throwRuntimeError(); } catch (const std::exception &) { throw; } }
    catch (const std::runtime_error &e) { caught = std::string(e.what()) == "boom"; }
    check("rethrow keeps the original type", caught);

    printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
