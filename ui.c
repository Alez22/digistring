/* SPDX-License-Identifier: MIT
 *
 * STRING's machine descriptor and its SRC page, as machine-pages draws it
 * (elekloader docs/ADAPTING.md, "Machine pages").
 *
 * STRING keeps SLICE's eight parameter slots (params machine 3), so its
 * values are saved, p-locked and reached by MIDI like any stock parameter.
 * The page gives each slot STRING's own name, range, default and text;
 * machine-pages applies them only while the sound's machine is STRING.
 * See digitakt.c for how the engine reads each slot.
 */
#include <stdint.h>
#include "cm_ui.h"

#define KS_MACHINE 8
#define PARAM_BR 0x86 /* SLICE's plain 0..127 knob: the UI record we borrow */
#define V(n) ((int32_t)(n) << 8) /* a value in 8.8, as the firmware keeps it */

static const char ks_exc_names[4][6] = { "BOW", "HIT", "NOISE", "PLUCK" };

#include "ks_icons.inc"

/* Firmware addresses that moved in OS 1.54 (mod.json's port builds with
 * -DDIGISTRING_OS154): the stock Bitmap vtable and the blit routine. */
#ifdef DIGISTRING_OS154
#define KS_BMP_VT 0x401b7734u
#define KS_BLIT_AT 0x400c2b88u
#else
#define KS_BMP_VT 0x401b73b4u
#define KS_BLIT_AT 0x400c2960u
#endif

/* The firmware's Bitmap object, as its blit (KS_BLIT_AT) reads it:
 * vtable, width, height, longs per column, pixels by column, mask, 0. */
struct ks_bitmap {
    uint32_t vtable;
    int32_t width, height, stride;
    const uint32_t *pixels, *mask;
    int32_t unused;
};

static const struct ks_bitmap ks_exc_icons[4] = {
    { KS_BMP_VT, KS_ICON_W, KS_ICON_H, 1, ks_icon_bow, ks_icon_mask, 0 },
    { KS_BMP_VT, KS_ICON_W, KS_ICON_H, 1, ks_icon_hit, ks_icon_mask, 0 },
    { KS_BMP_VT, KS_ICON_W, KS_ICON_H, 1, ks_icon_noise, ks_icon_mask, 0 },
    { KS_BMP_VT, KS_ICON_W, KS_ICON_H, 1, ks_icon_pluck, ks_icon_mask, 0 },
};

/* Menu icon: a vibrating string between two end points. Pixels by column,
 * bit 31 the bottom row (the icon is symmetric); all 11 x 7 opaque.
 *   ...........
 *   ...#####...
 *   .##.....##.
 *   #.........#
 *   .##.....##.
 *   ...#####...
 *   ........... */
static const uint32_t ks_menu_px[11] = {
    0x10000000, 0x28000000, 0x28000000, 0x44000000, 0x44000000, 0x44000000,
    0x44000000, 0x44000000, 0x28000000, 0x28000000, 0x10000000,
};
static const uint32_t ks_menu_mask[11] = {
    0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000,
    0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000,
};
static const struct ks_bitmap ks_menu_icon = {
    KS_BMP_VT, 11, 7, 1, ks_menu_px, ks_menu_mask, 0
};

/* blit(dst, src, x, y, centre): with centre set, (x, y) is the middle. */
typedef void (*ks_blit_fn)(void *dst, const void *src, int32_t x, int32_t y,
                           int32_t centre);
#define KS_BLIT ((ks_blit_fn)KS_BLIT_AT)

/** The stored 8.8 value as 0..127. */
static uint32_t ks_value(int32_t value)
{
    return ((uint32_t)value >> 8) & 0x7fu;
}

static void ks_copy(char *out, const char *in)
{
    uint32_t i = 0;
    while (in[i]) { out[i] = in[i]; ++i; }
    out[i] = 0;
}

/** 0..127 as decimal text, without a divide-heavy printf. */
static void ks_number(char *out, uint32_t n)
{
    char *p = out;
    if (n >= 100) { *p++ = '1'; n -= 100; *p++ = (char)('0' + n / 10); }
    else if (n >= 10) *p++ = (char)('0' + n / 10);
    *p++ = (char)('0' + n % 10);
    *p = 0;
}

/**
 * @brief Value text of a plain number control (STIFF, SOFT, TONE, DECAY).
 * @return 1: always written.
 */
int32_t digistring_fmt_number(char *buf, int32_t value, int32_t ctx,
                              int32_t machine)
{
    (void)ctx;
    (void)machine;
    ks_number(buf, ks_value(value));
    return 1;
}

/** @brief POS's text: 0 is OFF (no pluck position comb). */
int32_t digistring_fmt_pos(char *buf, int32_t value, int32_t ctx,
                           int32_t machine)
{
    uint32_t n = ks_value(value);
    (void)ctx;
    (void)machine;
    if (n == 0) ks_copy(buf, "OFF");
    else ks_number(buf, n);
    return 1;
}

/** @brief EXC's text: the exciter's name. */
int32_t digistring_fmt_exc(char *buf, int32_t value, int32_t ctx,
                           int32_t machine)
{
    (void)ctx;
    (void)machine;
    ks_copy(buf, ks_exc_names[ks_value(value) & 3u]);
    return 1;
}

/** Scale a small range onto the borrowed 0..127 knob graphic. */
static int32_t ks_knob_scaled(int32_t value, uint32_t mul)
{
    uint32_t scaled = ks_value(value) * mul;
    return V(scaled > 127u ? 127u : scaled);
}

/** @brief POS (0..64) shown on the 0..127 knob. */
int32_t digistring_gfx_pos(int32_t value)
{
    return ks_knob_scaled(value, 2u);
}

/** @brief TONE (0..4) shown on the 0..127 knob. */
int32_t digistring_gfx_tone(int32_t value)
{
    return ks_knob_scaled(value, 32u);
}

/**
 * @brief Draw EXC's exciter icon in place of a knob.
 * @param bmp the page's Bitmap; the knob spans x + 1..x + 17, y..y + 16.
 * @return 1: always drawn.
 */
static int32_t ks_draw_exc(void *bmp, int32_t x, int32_t y, int32_t value,
                           int32_t flag)
{
    (void)flag;
    KS_BLIT(bmp, &ks_exc_icons[ks_value(value) & 3u], x + 9, y + 8, 1);
    return 1;
}

/* A control with its own range and default, on BR's plain knob. graph
 * scales a range below 0..127 onto that knob's graphic (0: none). */
#define KS_KNOB(n, ln, hi, dflt, text, graph)                              \
    { .name = (n), .lname = (ln), .look = PARAM_BR,                       \
      .flags = CM_RANGE | CM_DEFAULT, .min = 0, .max = V(hi),             \
      .def = V(dflt), .fmt = (text), .u.gfx = (graph) }

/* Knobs A..H. TUNE stays stock; SAMP is hidden, STRING plays no sample.
 * EXC keeps the order BOW, HIT, NOISE, PLUCK so saved sounds keep their
 * exciter; its default is PLUCK. */
static const struct cm_ui ks_page = {
    .abi = cm_ui_v31,
    .page_from = 3,
    .group = "STRG",
    .knob = {
        { 0 },                                                    /* A TUNE */
        { .name = "EXC", .lname = "Exciter", .look = PARAM_BR,    /* B */
          .flags = CM_RANGE | CM_DEFAULT | CM_DRAW, .min = 0,
          .max = V(3), .def = V(3), .fmt = digistring_fmt_exc,
          .u.draw = ks_draw_exc },
        KS_KNOB("STIFF", "Stiffness", 127, 0, digistring_fmt_number, 0),
        { .flags = CM_HIDDEN },                                   /* D SAMP */
        KS_KNOB("POS", "Position", 64, 0, digistring_fmt_pos,
                digistring_gfx_pos),
        KS_KNOB("SOFT", "Softness", 127, 0, digistring_fmt_number, 0),
        KS_KNOB("TONE", "Tone", 4, 0, digistring_fmt_number,
                digistring_gfx_tone),
        KS_KNOB("DECAY", "Decay", 127, 100, digistring_fmt_number, 0),
    },
};

/* core_machines descriptor: id, name, short name, menu icon, params
 * machine (3: SLICE's eight slots), render machine (its own id: an empty
 * voice window), then the page's tag and the page. */
const struct cm_machine digistring_machine = {
    KS_MACHINE, "STRING", "STRG", &ks_menu_icon, 3, KS_MACHINE,
    CM_UI_TAG, &ks_page,
};
