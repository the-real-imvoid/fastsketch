#ifndef SELECTION_H
#define SELECTION_H
#include "stdint.h"
#include <SDL2/SDL.h>

extern int sel_x;
extern int sel_y;
extern int sel_width;
extern int sel_height;
extern int ants_offset;

extern uint32_t *selection;
extern uint32_t *clipboard;
extern int clip_width;
extern int clip_height;

void make_selection(int x1, int y1, int x2, int y2, uint32_t *canvas, int width, SDL_Renderer *renderer);
void init_selection_tex_from_buffer(SDL_Renderer *renderer);
void place_selection(int x, int y, uint32_t *canvas, int width, int height);
void yeet_old_area(uint32_t *canvas, int width, int height);
void draw_preview(SDL_Renderer *renderer, int x, int y, float zoom, int cam_x, int cam_y);
void draw_marching_ants_specific(SDL_Renderer *r, float zoom, int cam_x, int cam_y, int x, int y, int w, int h);
void draw_marching_ants(SDL_Renderer *r, float zoom, int cam_x, int cam_y);
void step_ant_state(void);
void free_selection(void);
void free_clipboard(void);
void copy(void);

#endif
