#include <stdio.h>
#include <stdlib.h>
#include <webp/decode.h>
int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "sample.webp";
    FILE *fp = fopen(path, "rb");
    if (!fp) { printf("open failed\n"); return 1; }
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    unsigned char *buf = malloc(len);
    fread(buf, 1, len, fp);
    fclose(fp);
    int w, h;
    unsigned char *pix = WebPDecodeRGBA(buf, len, &w, &h);
    if (!pix) { printf("decode failed\n"); return 1; }
    printf("webp OK: %dx%d first_px=%d,%d,%d,%d\n", w, h, pix[0], pix[1], pix[2], pix[3]);
    WebPFree(pix);
    free(buf);
    return 0;
}
