/* SPDX-License-Identifier: MIT
 *
 * The parts of the machine-pages ABI that STRING uses: a machine
 * descriptor with a page (tag "UI30"), the page and its knobs.
 * Layouts as documented in elekloader docs/ADAPTING.md, "Machine pages";
 * written here so STRING stays MIT without including a GPL header.
 * Offsets are for the 32-bit target and checked at compile time there.
 */
#ifndef DIGISTRING_CM_UI_H
#define DIGISTRING_CM_UI_H

#include <stdint.h>

#define CM_UI_TAG 0x55493330u /* "UI30": the descriptor has a page */

/* Knob flags (cm_knob.flags). */
#define CM_HIDDEN 1u   /* blank knob: it shows and turns nothing */
#define CM_RANGE 2u    /* min and max replace the stock range */
#define CM_DEFAULT 4u  /* def replaces the stock default */
#define CM_DRAW 16u    /* draw() draws the knob (needs a cm_ui_v31 page) */

/** A value's text into buf: nonzero if written, 0 for the stock text.
 *  ctx 0: under a turning knob (about 5 chars); 1: the pop-up (up to 15). */
typedef int32_t (*cm_fmt)(char *buf, int32_t value, int32_t ctx,
                          int32_t machine);
/** The value the knob graphic shows, from the stored one (both 8.8). */
typedef int32_t (*cm_gfx)(int32_t value);
/** Draw the knob graphic into bmp (x + 1..x + 17, y..y + 16): nonzero if
 *  drawn, 0 for the stock graphic. */
typedef int32_t (*cm_draw)(void *bmp, int32_t x, int32_t y, int32_t value,
                           int32_t flag);

struct cm_knob {                /* 40 bytes on the target */
    const char *name;           /* +0  SRC page label; 0: stock */
    const char *lname;          /* +4  pop-up name; 0: stock */
    const char *lfo;            /* +8  LFO DEST box name; 0: name */
    const char *lfo_long;       /* +12 LFO DEST list name; 0: lname */
    uint8_t look;               /* +16 param id whose UI record it borrows */
    uint8_t flags;              /* +17 CM_* */
    uint16_t reserved;          /* +18 0 */
    int32_t min, max, def;      /* +20 8.8, with CM_RANGE / CM_DEFAULT */
    cm_fmt fmt;                 /* +32 value text; 0: stock */
    union {
        cm_gfx gfx;             /* +36 graphic value; 0: the value itself */
        cm_draw draw;           /* +36 with CM_DRAW */
    } u;
};

struct cm_ui {                  /* 336 bytes on the target */
    const void *abi;            /* +0  cm_ui_v31: links only beside it */
    uint8_t page_from;          /* +4  stock page it copies: 3 SLICE */
    uint8_t flags;              /* +5  0 */
    uint16_t reserved;          /* +6  0 */
    const char *group;          /* +8  LFO page group; 0: stock */
    void (*on_switch)(int32_t track, int32_t from, uint8_t *sound);
                                /* +12 a track turns to it; 0: none */
    struct cm_knob knob[8];     /* +16 A..H */
};

struct cm_machine {             /* what core_machines points to */
    int32_t id;                 /* +0  4..127 */
    const char *name, *sname;   /* +4, +8 */
    const void *icon;           /* +12 an 11 x 7 Bitmap */
    int32_t params, render;     /* +16, +20 */
    uint32_t tag;               /* +24 CM_UI_TAG */
    const struct cm_ui *ui;     /* +28 */
};

/* Exported by machine-pages 1.1 and newer. */
extern const char cm_ui_v31[];

#if UINTPTR_MAX == 0xffffffffu
_Static_assert(sizeof(struct cm_knob) == 40, "cm_knob layout");
_Static_assert(sizeof(struct cm_ui) == 336, "cm_ui layout");
_Static_assert(sizeof(struct cm_machine) == 32, "cm_machine layout");
#endif

#endif
