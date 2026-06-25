//a nice header only library for working with RGBA colors in C/C++.
//It defines a macro for creating RGBA values from individual red, green, blue,
//and alpha components, as well as some predefined colors like white, black, red, green, and blue.
#ifndef RGBA_H
#define RGBA_H

#include <stdint.h>
#include <SDL2/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t RGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void rgba_init();

#ifdef __cplusplus
}
#endif

#define ERASER 0xFFFFFFFF

#endif