// Second image, so unwinding has to find EH info through _dyld_find_unwind_sections.
#include "throwlib.h"
void throwFromLib(int n) { if (n) throw LibError("thrown in dylib"); }
int catchInLib(void (*fn)())
{
    try { fn(); } catch (const std::logic_error &) { return 1; } catch (...) { return 2; }
    return 0;
}
