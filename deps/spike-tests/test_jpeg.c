#include <stdio.h>
#include <stdlib.h>
#include <jpeglib.h>
#include <setjmp.h>
int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "sample.jpg";
    FILE *fp = fopen(path, "rb");
    if (!fp) { printf("open failed\n"); return 1; }
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, fp);
    jpeg_read_header(&cinfo, TRUE);
    jpeg_start_decompress(&cinfo);
    int rowbytes = cinfo.output_width * cinfo.output_components;
    unsigned char *row = malloc(rowbytes);
    JSAMPROW rowptr[1] = { row };
    int first_r = 0, first_g = 0, first_b = 0;
    int y = 0;
    while (cinfo.output_scanline < cinfo.output_height) {
        jpeg_read_scanlines(&cinfo, rowptr, 1);
        if (y == 0) { first_r = row[0]; first_g = row[1]; first_b = row[2]; }
        y++;
    }
    printf("jpeg OK: %ux%u comp=%d first_px=%d,%d,%d\n",
           cinfo.output_width, cinfo.output_height, cinfo.output_components,
           first_r, first_g, first_b);
    free(row);
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(fp);
    return 0;
}
