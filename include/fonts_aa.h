#ifndef FONTS_AA_H
#define FONTS_AA_H

#include <stdint.h>

#define AA_FONT_BPP 4

typedef struct {
    uint8_t height;
    uint8_t count;
    const char *chars;          /* glyph order; 0xB0 = degree */
    const uint8_t *widths;
    const uint16_t *offsets;     /* into data */
    const uint8_t *data;         /* 4 bpp, row-major, LSB first */
} aa_font_t;

extern const aa_font_t font_big;   /* 26 px tall, 3315 bytes */
extern const aa_font_t font_mid;   /* 13 px tall, 4467 bytes */
extern const aa_font_t font_small;   /* 11 px tall, 3103 bytes */

#endif
