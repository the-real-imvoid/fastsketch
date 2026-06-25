#ifdef __cplusplus
extern "C" {
    #endif
    #include <stdint.h>
    #include <stdbool.h>
    #include <SDL2/SDL.h>
    void resize_canvas(int new_w, int new_h,
                       uint32_t **canvas,
                       SDL_Renderer *renderer,
                       SDL_Texture **texture);
    void put_pixel(uint32_t pixels[], int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    void drawops_set_size(int w, int h);
    void apply_brush_circle(int cx, int cy, int radius, uint32_t color, uint32_t *canvas, int width, int height);
    void fill(int cx, int cy, int width, int height, uint32_t color, uint32_t *canvas);
    void reset_canvas(uint32_t *canvas, int width, int height);
    void dirty_reset(void);
    bool dirty_valid(void);
    SDL_Rect dirty_get(void);
    void dirty_mark_rect(int x1, int y1, int x2, int y2);
    void apply_airbrush(int cx, int cy, int radius, uint32_t color, uint32_t *canvas, int width, int height, int density);
    void apply_brightness_tool(int cx, int cy, int radius, uint32_t *canvas,
                               int width, int height, int mode, float amount);
    void start_stroke(int width, int height);
    void apply_filter_tool(int cx, int cy, int radius, uint32_t *canvas, int width, int height, int mode);
    void apply_smudge_tool(int cx, int cy, int last_cx, int last_cy, int radius, uint32_t *canvas, int width, int height, float strength);
    void apply_blend_tool(int cx, int cy, int radius, uint32_t *canvas, int width, int height, float strength);
    void apply_pencil(int mouse_x, int mouse_y, int radius, uint32_t brush_color, uint32_t* canvas, int canvas_width, int canvas_height);
    #ifdef __cplusplus
}
#endif
