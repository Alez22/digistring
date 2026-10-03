/* SPDX-License-Identifier: MIT */
/* Host tests for ui.c: STRING answers only on its own page and slots. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern volatile int32_t ks_page_m;
const char *ks_ui_label(int32_t param, int32_t long_name);
char *ks_ui_text(int32_t param, int32_t value, char *out);
int32_t ks_ui_record(int32_t param);
int32_t ks_ui_knob(int32_t param, int32_t value);
const void *ks_ui_icon(int32_t param, int32_t value);
const char *ks_ui_lfo_name(int32_t desc_offset, int32_t long_name);
const char *ks_ui_lfo_group(int32_t param);
const char *ks_ui_lfo_group_at(int32_t desc_offset);

int main(void)
{
    char out[12];
    ks_page_m = 7; /* digisophie's page: never ours */
    assert(!ks_ui_label(0x8b, 0));
    assert(!ks_ui_text(0x85, 3 << 8, out));
    assert(ks_ui_record(0x85) == -1);
    assert(ks_ui_knob(0x85, 3 << 8) == 3 << 8);

    ks_page_m = 8;
    assert(!ks_ui_label(0x84, 0));          /* TUNE stays stock */
    assert(!ks_ui_label(0x87, 1));          /* SAMP stays stock */
    assert(!ks_ui_label(0x83, 0));          /* not an SRC slot */
    assert(!ks_ui_label(0x8c, 0));
    assert(!strcmp(ks_ui_label(0x85, 0), "EXC"));
    assert(!strcmp(ks_ui_label(0x8b, 0), "DECAY"));
    assert(!strcmp(ks_ui_label(0x8b, 1), "Decay"));
    assert(!strcmp(ks_ui_text(0x85, 3 << 8, out), "PLUCK"));
    assert(!strcmp(ks_ui_text(0x85, 0, out), "BOW"));
    assert(!strcmp(ks_ui_text(0x88, 0, out), "OFF"));
    assert(!strcmp(ks_ui_text(0x88, 64 << 8, out), "64"));
    assert(!strcmp(ks_ui_text(0x8b, 127 << 8, out), "127"));
    assert(!strcmp(ks_ui_text(0x8b, 100 << 8, out), "100"));
    assert(!strcmp(ks_ui_text(0x86, 7 << 8, 0), "7")); /* popup buffer */
    assert(ks_ui_record(0x8a) == 0x86);
    assert(ks_ui_record(0x84) == -1);
    /* Raw values, 0..127 in the high byte, as the knob routine gets them. */
    assert(ks_ui_knob(0x85, 3 << 8) == 126 << 8);
    assert(ks_ui_knob(0x85, 1 << 8) == 42 << 8);
    assert(ks_ui_knob(0x86, 77 << 8) == 77 << 8);  /* STIFF: unchanged */
    assert(ks_ui_knob(0x88, 64 << 8) == 127 << 8);
    assert(ks_ui_knob(0x89, 63 << 8) == 126 << 8);
    assert(ks_ui_knob(0x8a, 2 << 8) == 64 << 8);
    assert(ks_ui_knob(0x8a, 4 << 8) == 127 << 8);
    assert(ks_ui_knob(0x8b, 100 << 8) == 100 << 8);
    assert(ks_ui_knob(0x84, 0x4000) == 0x4000);    /* TUNE: stock */

    /* EXC draws one of four icons; every other control keeps its knob. */
    assert(ks_ui_icon(0x85, 0) && ks_ui_icon(0x85, 3 << 8));
    assert(ks_ui_icon(0x85, 0) != ks_ui_icon(0x85, 3 << 8));
    assert(!ks_ui_icon(0x8b, 100 << 8));
    assert(!ks_ui_icon(0x84, 0x4000));

    /* LFO destinations index descriptors by param * 52. */
    assert(!strcmp(ks_ui_lfo_name(0x8b * 52, 0), "DECAY"));
    assert(!strcmp(ks_ui_lfo_name(0x85 * 52, 1), "Exciter"));
    assert(!ks_ui_lfo_name(0x84 * 52, 0));   /* TUNE keeps its name */
    assert(!ks_ui_lfo_name(-52, 0));
    assert(!strcmp(ks_ui_lfo_group(0x84), "STRG")); /* but STRG group */
    assert(!strcmp(ks_ui_lfo_group_at(0x87 * 52), "STRG"));
    assert(!ks_ui_lfo_group(0x8c));
    assert(!ks_ui_lfo_group(0x10));

    ks_page_m = 2; /* a stock machine's page */
    assert(!ks_ui_lfo_group(0x84));
    assert(!ks_ui_lfo_name(0x8b * 52, 0));
    assert(!ks_ui_icon(0x85, 0));
    assert(!ks_ui_label(0x8b, 0));
    puts("ui: all tests passed");
    return 0;
}
