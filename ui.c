/* SPDX-License-Identifier: MIT
 *
 * STRING's SRC page: names, value texts and knob drawing for SLICE's
 * parameter slots while a STRING sound's page is shown.
 *
 * The stubs in glue.s call these from inside stock UI routines, at
 * instructions after digisophie's entry hooks, so both mods chain. Each
 * helper answers only for STRING's page and its own controls, and returns
 * "not ours" otherwise so the stock code carries on. Ranges and defaults
 * stay SLICE's (see digitakt.c for the control map).
 */
#include <stdint.h>

#define KS_MACHINE 8
/* SLICE's SRC parameter IDs: A..H = 0x84..0x8b. */
#define PARAM_A 0x84
#define PARAM_BR 0x86 /* a plain 0..127 knob: the record we borrow */
#define SLOTS 8

enum ks_ui_kind {
    UI_STOCK = 0, /* leave the stock name, text and knob (TUNE, SAMP) */
    UI_EXC,       /* 0..3 exciter names */
    UI_NUMBER,    /* the stored value as a number */
    UI_POS        /* the stored value, with 0 shown as OFF */
};

struct ks_ui_slot {
    const char *short_name;
    const char *long_name;
    uint8_t kind;
    uint8_t knob_mul; /* stored value * knob_mul spans the 0..127 knob */
};

static const struct ks_ui_slot ks_slots[SLOTS] = {
    { 0, 0, UI_STOCK, 0 },                       /* A TUNE */
    { "EXC", "Exciter", UI_EXC, 42 },            /* B 0..3 */
    { "STIFF", "Stiffness", UI_NUMBER, 1 },      /* C 0..127 */
    { 0, 0, UI_STOCK, 0 },                       /* D SAMP */
    { "POS", "Position", UI_POS, 2 },            /* E 0..64 */
    { "SOFT", "Softness", UI_NUMBER, 2 },        /* F 0..63 */
    { "TONE", "Tone", UI_NUMBER, 32 },           /* G 0..4 */
    { "DECAY", "Decay", UI_NUMBER, 1 },          /* H 0..127 */
};

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
 * width, height, longs per column, pixels by column, mask. The vtable is
 * the stock Bitmap one, as for the menu icon in glue.s. */
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

/* blit(dst, src, x, y, centre): with centre set, (x, y) is the middle. */
typedef void (*ks_blit_fn)(void *dst, const void *src, int32_t x, int32_t y,
                           int32_t centre);
#define KS_BLIT ((ks_blit_fn)KS_BLIT_AT)

/* The machine whose SRC page layout was last asked for; glue.s writes it
 * from the two callers of the layout routine. */
volatile int32_t digistring_page_m;
/* Popup text buffer, returned to the firmware like its own. */
static char ks_txt[12];

/** STRING's slot for a parameter ID on STRING's page, or 0 if none. */
static const struct ks_ui_slot *ks_ui_slot(int32_t param)
{
    uint32_t index = (uint32_t)(param - PARAM_A);
    if (digistring_page_m != KS_MACHINE || index >= SLOTS) return 0;
    if (ks_slots[index].kind == UI_STOCK) return 0;
    return &ks_slots[index];
}

static char *ks_copy(char *out, const char *in)
{
    uint32_t i = 0;
    while (in[i]) { out[i] = in[i]; ++i; }
    out[i] = 0;
    return out;
}

/** 0..127 as decimal text, without a divide-heavy printf. */
static char *ks_number(char *out, uint32_t n)
{
    char *p = out;
    if (n >= 100) { *p++ = '1'; n -= 100; *p++ = (char)('0' + n / 10); }
    else if (n >= 10) *p++ = (char)('0' + n / 10);
    *p++ = (char)('0' + n % 10);
    *p = 0;
    return out;
}

/**
 * @brief Short or long name for a parameter, or 0 for the stock one.
 * @param long_name nonzero for the long (popup/menu) name.
 */
const char *digistring_ui_label(int32_t param, int32_t long_name)
{
    const struct ks_ui_slot *slot = ks_ui_slot(param);
    if (!slot) return 0;
    return long_name ? slot->long_name : slot->short_name;
}

/** Size of one stock parameter descriptor: LFO code indexes by bytes. */
#define DESCRIPTOR_SIZE 52

/**
 * @brief LFO destination name from a descriptor byte offset, or 0.
 * The LFO page, its popup and its overview index the descriptor table
 * by param * 52 rather than by param.
 */
const char *digistring_ui_lfo_name(int32_t desc_offset, int32_t long_name)
{
    if (desc_offset < 0) return 0;
    return digistring_ui_label(desc_offset / DESCRIPTOR_SIZE, long_name);
}

/**
 * @brief Group shown before an LFO destination ("STRG" in STRG:Decay)
 * for any of STRING's eight SRC slots, TUNE and SAMP included, or 0.
 */
const char *digistring_ui_lfo_group(int32_t param)
{
    uint32_t index = (uint32_t)(param - PARAM_A);
    if (digistring_page_m != KS_MACHINE || index >= SLOTS) return 0;
    return "STRG";
}

/** As digistring_ui_lfo_group, from a descriptor byte offset. */
const char *digistring_ui_lfo_group_at(int32_t desc_offset)
{
    if (desc_offset < 0) return 0;
    return digistring_ui_lfo_group(desc_offset / DESCRIPTOR_SIZE);
}

/**
 * @brief Write a parameter's value text into `out`.
 * @param value the stored value, 0..127 in its high byte, as the firmware
 *              passes it to its own formatters.
 * @return out, or 0 to let the stock formatter run.
 */
char *digistring_ui_text(int32_t param, int32_t value, char *out)
{
    const struct ks_ui_slot *slot = ks_ui_slot(param);
    uint32_t n = ((uint32_t)value >> 8) & 0x7fu;
    if (!slot) return 0;
    if (!out) out = ks_txt;
    switch (slot->kind) {
    case UI_EXC: return ks_copy(out, ks_exc_names[n & 3u]);
    case UI_POS: return n ? ks_number(out, n) : ks_copy(out, "OFF");
    default: return ks_number(out, n);
    }
}

/**
 * @brief The parameter whose UI record (knob style) to use, or -1.
 * SLICE's own records draw PLAY, SLICE, LEN and GRID in their own ways;
 * STRING's controls borrow BR's plain knob instead, as digisophie does.
 */
int32_t digistring_ui_record(int32_t param)
{
    return ks_ui_slot(param) ? PARAM_BR : -1;
}

/**
 * @brief The icon drawn instead of a knob, or 0 to draw the knob.
 * @param value the stored value, 0..127 in its high byte.
 */
const void *digistring_ui_icon(int32_t param, int32_t value)
{
    const struct ks_ui_slot *slot = ks_ui_slot(param);
    if (!slot || slot->kind != UI_EXC) return 0;
    return &ks_exc_icons[((uint32_t)value >> 8) & 3u];
}

/**
 * @brief Draw a control's icon in place of its knob.
 * @param screen the Bitmap the page is drawn into.
 * @param x, y the knob's corner as the stock knob drawer gets it: the page
 *             clears a 17 x 17 box from (x + 1, y) for an inactive knob.
 * @return 1 if drawn (skip the stock knob), 0 otherwise.
 */
int32_t digistring_ui_draw(int32_t param, int32_t value, void *screen,
                   int32_t x, int32_t y)
{
    const void *icon = digistring_ui_icon(param, value);
    if (!icon) return 0;
    KS_BLIT(screen, icon, x + 9, y + 8, 1);
    return 1;
}

/**
 * @brief Scale a knob value so the borrowed 0..127 knob spans the slot's
 * SLICE range. Other parameters pass through.
 * @param value the stored value, 0..127 in its high byte (the knob routine
 *              gets the raw value, as the value-text routine does).
 */
int32_t digistring_ui_knob(int32_t param, int32_t value)
{
    const struct ks_ui_slot *slot = ks_ui_slot(param);
    uint32_t scaled;
    if (!slot) return value;
    scaled = (((uint32_t)value >> 8) & 0x7fu) * slot->knob_mul;
    return (int32_t)((scaled > 127u ? 127u : scaled) << 8);
}
