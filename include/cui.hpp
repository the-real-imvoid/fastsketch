#pragma once

#include <SDL2/SDL.h>
#include <stdint.h>
#include "tool.h"
#include "drawops.h"

#ifdef __cplusplus
extern "C" {
#endif

extern bool minimised;
bool ui_wants_mouse(void); // true when ImGui wants the mouse (don't paint/resize/pan through UI)
int get_brush_size(void);
int get_density(void);
void ui_init(SDL_Window *window, SDL_Renderer *renderer);
void ui_process_event(SDL_Event *event);
void ui_begin_frame(Toolbar *tb, SDL_Window *window, int win_w, int win_h,
                    int *width, int *height,
                    uint32_t **canvas,
                    SDL_Renderer *renderer, SDL_Texture **texture);
void ui_end_frame(void);
void ui_shutdown(void);
void set_color(uint32_t c);
uint32_t get_secondary_color(void);
uint32_t get_color(void);
SDL_HitTestResult SDLCALL hit_test(SDL_Window *win, const SDL_Point *area, void *data);

#ifdef __cplusplus
}
#endif
