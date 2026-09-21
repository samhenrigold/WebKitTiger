#include <png.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "sample.png";
    FILE *fp = fopen(path, "rb");
    if (!fp) { printf("open failed\n"); return 1; }
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png_create_info_struct(png);
    if (setjmp(png_jmpbuf(png))) { printf("png error\n"); return 1; }
    png_init_io(png, fp);
    png_read_info(png, info);
    int w = png_get_image_width(png, info);
    int h = png_get_image_height(png, info);
    png_set_expand(png);
    png_set_add_alpha(png, 0xff, PNG_FILLER_AFTER);
    png_read_update_info(png, info);
    png_bytep row = malloc(png_get_rowbytes(png, info));
    png_read_row(png, row, NULL);
    printf("png OK: %dx%d first_px=%d,%d,%d,%d\n", w, h, row[0], row[1], row[2], row[3]);
    free(row);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    return 0;
}
