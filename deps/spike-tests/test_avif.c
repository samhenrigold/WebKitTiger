#include <stdio.h>
#include <avif/avif.h>
int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "sample.avif";
    avifDecoder *decoder = avifDecoderCreate();
    if (avifDecoderSetIOFile(decoder, path) != AVIF_RESULT_OK) { printf("io failed\n"); return 1; }
    if (avifDecoderParse(decoder) != AVIF_RESULT_OK) { printf("parse failed: %s\n", decoder->diag.error); return 1; }
    if (avifDecoderNextImage(decoder) != AVIF_RESULT_OK) { printf("decode failed\n"); return 1; }
    avifImage *img = decoder->image;
    printf("avif OK: %ux%u depth=%d\n", img->width, img->height, img->depth);
    avifDecoderDestroy(decoder);
    return 0;
}
