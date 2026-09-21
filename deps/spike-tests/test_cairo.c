#include <stdio.h>
#include <cairo.h>
int main(void) {
    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 16, 16);
    cairo_t *cr = cairo_create(surf);
    cairo_set_source_rgb(cr, 1, 0, 0);
    cairo_rectangle(cr, 2, 2, 10, 10);
    cairo_fill(cr);
    cairo_surface_flush(surf);
    unsigned char *data = cairo_image_surface_get_data(surf);
    int stride = cairo_image_surface_get_stride(surf);
    unsigned char *px = data + 8 * stride + 8 * 4;
    printf("cairo OK: status=%s pixel(8,8)=%02x%02x%02x%02x\n",
           cairo_status_to_string(cairo_surface_status(surf)), px[0], px[1], px[2], px[3]);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    return 0;
}
