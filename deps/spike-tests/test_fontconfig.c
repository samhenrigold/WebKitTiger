#include <stdio.h>
#include <fontconfig/fontconfig.h>
int main(void) {
    FcConfig *cfg = FcInitLoadConfig();
    if (!cfg) { printf("fontconfig init failed\n"); return 1; }
    printf("fontconfig OK: version=%d\n", FcGetVersion());
    FcPattern *pat = FcPatternCreate();
    FcPatternAddString(pat, FC_FAMILY, (const FcChar8 *)"DejaVu Sans");
    FcDefaultSubstitute(pat);
    printf("fontconfig pattern created OK\n");
    FcPatternDestroy(pat);
    FcConfigDestroy(cfg);
    return 0;
}
