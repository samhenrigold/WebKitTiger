#include <unicode/unistr.h>
#include <unicode/coll.h>
#include <cstdio>
using namespace icu;
int main() {
    UnicodeString a = UnicodeString::fromUTF8("héllo");
    UnicodeString b = UnicodeString::fromUTF8("world");
    UnicodeString c = a + " " + b;
    std::string out;
    c.toUTF8String(out);
    printf("icu OK: %s (len=%d)\n", out.c_str(), c.length());

    UErrorCode status = U_ZERO_ERROR;
    Collator *coll = Collator::createInstance(status);
    if (U_FAILURE(status)) { printf("collator failed: %s\n", u_errorName(status)); return 1; }
    UCollationResult res = (UCollationResult)coll->compare(UnicodeString("apple"), UnicodeString("banana"));
    printf("icu collate OK: %d\n", (int)res);
    delete coll;
    return 0;
}
