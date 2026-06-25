#ifndef UNDO_H
#define UNDO_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>

typedef struct {
    char *data;
    int   compressed_sz;
    int   w, h;    /* full canvas size */
    int   rx, ry;  /* dirty region origin */
    int   rw, rh;  /* dirty region size (rw==w && rh==h means full canvas) */
} Snapshot;

void undo_init(void);
void take_snapshot(uint32_t *canvas, int width, int height);
void undo(uint32_t **canvas, int *width, int *height);
void redo(uint32_t **canvas, int *width, int *height);
void undo_cleanup(void);

extern Snapshot *snapshots;
extern int count;
extern int where;

#ifdef __cplusplus
}
#endif
#endif
