#ifndef LOGO_H
#define LOGO_H

#include <stdint.h>

/* 32-colour run-length coded image: each byte is (run-1)<<5 | palette index
 * (runs of 1..8 pixels), row-major. Palette index 0 is always black (the
 * screen background). */
typedef struct {
    uint8_t width, height;
    const uint16_t *palette;
    const uint8_t *rle;
    uint16_t rle_len;
} logo_t;

extern const logo_t logo_small;   /* start-up screen */
extern const logo_t logo_large;   /* heater-off screen */

#endif
