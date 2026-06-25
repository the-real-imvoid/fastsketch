#ifndef CURSOR_H
#define CURSOR_H
#include <SDL2/SDL.h>
#include <stdint.h>

void cursor_init(SDL_Renderer *renderer);
void render_brush_cursor(int gx, int gy, int radius, SDL_Renderer *renderer, float zoom, uint32_t *canvas,
int cx, int cy, int width, int camera_x, int camera_y);
void render_fill_cursor(int gx, int gy, SDL_Renderer *renderer);

#endif
