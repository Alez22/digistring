/* SPDX-License-Identifier: MIT */
/* Host tests for ui.c: STRING answers only on its own page and slots. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern volatile int32_t digistring_page_m;
const char *digistring_ui_label(int32_t param, int32_t long_name);
char *digistring_ui_text(int32_t param, int32_t value, char *out);
int32_t digistring_ui_record(int32_t param);
int32_t digistring_ui_knob(int32_t param, int32_t value);
const void *digistring_ui_icon(int32_t param, int32_t value);
const char *digistring_ui_lfo_name(int32_t desc_offset, int32_t long_name);
const char *digistring_ui_lfo_group(int32_t param);
const char *digistring_ui_lfo_group_at(int32_t desc_offset);

int main(void)
{
    char out[12];
    digistring_page_m = 7; /* digisophie's page: never ours */
    assert(!digistring_ui_label(0x8b, 0));
    assert(!digistring_ui_text(0x85, 3 << 8, out));
    assert(digistring_ui_record(0x85) == -1);
    assert(digistring_ui_knob(0x85, 3 << 8) == 3 << 8);

    digistring_page_m = 8;
    assert(!digistring_ui_label(0x84, 0));          /* TUNE stays stock */
    assert(!digistring_ui_label(0x87, 1));          /* SAMP stays stock */
    assert(!digistring_ui_label(0x83, 0));          /* not an SRC slot */
    assert(!digistring_ui_label(0x8c, 0));
    assert(!strcmp(digistring_ui_label(0x85, 0), "EXC"));
    assert(!strcmp(digistring_ui_label(0x8b, 0), "DECAY"));
    assert(!strcmp(digistring_ui_label(0x8b, 1), "Decay"));
    assert(!strcmp(digistring_ui_text(0x85, 3 << 8, out), "PLUCK"));
    assert(!strcmp(digistring_ui_text(0x85, 0, out), "BOW"));
    assert(!strcmp(digistring_ui_text(0x88, 0, out), "OFF"));
    assert(!strcmp(digistring_ui_text(0x88, 64 << 8, out), "64"));
    assert(!strcmp(digistring_ui_text(0x8b, 127 << 8, out), "127"));
    assert(!strcmp(digistring_ui_text(0x8b, 100 << 8, out), "100"));
    assert(!strcmp(digistring_ui_text(0x86, 7 << 8, 0), "7")); /* popup buffer */
    assert(digistring_ui_record(0x8a) == 0x86);
    assert(digistring_ui_record(0x84) == -1);
    /* Raw values, 0..127 in the high byte, as the knob routine gets them. */
    assert(digistring_ui_knob(0x85, 3 << 8) == 126 << 8);
    assert(digistring_ui_knob(0x85, 1 << 8) == 42 << 8);
    assert(digistring_ui_knob(0x86, 77 << 8) == 77 << 8);  /* STIFF: unchanged */
    assert(digistring_ui_knob(0x88, 64 << 8) == 127 << 8);
    assert(digistring_ui_knob(0x89, 63 << 8) == 126 << 8);
    assert(digistring_ui_knob(0x8a, 2 << 8) == 64 << 8);
    assert(digistring_ui_knob(0x8a, 4 << 8) == 127 << 8);
    assert(digistring_ui_knob(0x8b, 100 << 8) == 100 << 8);
    assert(digistring_ui_knob(0x84, 0x4000) == 0x4000);    /* TUNE: stock */

    /* EXC draws one of four icons; every other control keeps its knob. */
    assert(digistring_ui_icon(0x85, 0) && digistring_ui_icon(0x85, 3 << 8));
    assert(digistring_ui_icon(0x85, 0) != digistring_ui_icon(0x85, 3 << 8));
    assert(!digistring_ui_icon(0x8b, 100 << 8));
    assert(!digistring_ui_icon(0x84, 0x4000));

    /* LFO destinations index descriptors by param * 52. */
    assert(!strcmp(digistring_ui_lfo_name(0x8b * 52, 0), "DECAY"));
    assert(!strcmp(digistring_ui_lfo_name(0x85 * 52, 1), "Exciter"));
    assert(!digistring_ui_lfo_name(0x84 * 52, 0));   /* TUNE keeps its name */
    assert(!digistring_ui_lfo_name(-52, 0));
    assert(!strcmp(digistring_ui_lfo_group(0x84), "STRG")); /* but STRG group */
    assert(!strcmp(digistring_ui_lfo_group_at(0x87 * 52), "STRG"));
    assert(!digistring_ui_lfo_group(0x8c));
    assert(!digistring_ui_lfo_group(0x10));

    digistring_page_m = 2; /* a stock machine's page */
    assert(!digistring_ui_lfo_group(0x84));
    assert(!digistring_ui_lfo_name(0x8b * 52, 0));
    assert(!digistring_ui_icon(0x85, 0));
    assert(!digistring_ui_label(0x8b, 0));
    puts("ui: all tests passed");
    return 0;
}
