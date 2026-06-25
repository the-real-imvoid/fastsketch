#ifndef SHAPES_H
#define SHAPES_H
#include <stdint.h>
#include <SDL2/SDL.h>
void put_rect(uint32_t *canvas, int x1, int y1, int x2, int y2, int t, uint32_t color1, uint32_t color2, int width, int height);
void draw_rect_preview(SDL_Renderer *renderer, int x1, int y1, int x2, int y2, int t, uint32_t c, uint32_t c2);
void bresenham(int x1, int y1, int x2, int y2,
               uint32_t c, uint32_t *canvas,
               int width, int height, int t);
void preview_line(SDL_Renderer* r,
                            int32_t x0, int32_t y0,
                            int32_t x1, int32_t y1,
                            int32_t thickness,
                            uint32_t c);
void put_oval(uint32_t *canvas,
              int cx, int cy,
              int rx, int ry,
              uint32_t fill_color,
              uint32_t outline_color,
              int outline_thickness,
              int width, int height);
void preview_oval(SDL_Renderer *renderer,
                  int cx, int cy,
                  int rx, int ry,
                  int thickness,
                  uint32_t fill_color,
                  uint32_t outline_color);

#endif
