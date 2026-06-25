#ifdef __cplusplus
extern "C" {
    #endif

#include "stdint.h"

void export_png(const char *filename, uint32_t *canvas, int width, int height);
void export_bmp(const char *filename, uint32_t *canvas, int width, int height);
void export_jpg(const char *filename, uint32_t *canvas, int width, int height);
uint32_t* load_image(const char *filename, int *out_width, int *out_height);

#ifdef __cplusplus
}
#endif
