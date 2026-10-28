/* SPDX-License-Identifier: MIT */
/* Host tests for ui.c: STRING's page as machine-pages reads it. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cm_ui.h"

/* machine-pages exports the marker; the host test stands in for it. */
const char cm_ui_v31[] = "machine-pages 3.1";

extern const struct cm_machine digistring_machine;
int32_t digistring_fmt_number(char *buf, int32_t value, int32_t ctx, int32_t machine);
int32_t digistring_fmt_pos(char *buf, int32_t value, int32_t ctx, int32_t machine);
int32_t digistring_fmt_exc(char *buf, int32_t value, int32_t ctx, int32_t machine);
int32_t digistring_gfx_pos(int32_t value);
int32_t digistring_gfx_tone(int32_t value);

#define V(n) ((int32_t)(n) << 8)

/** Check a knob's name, range and default. */
static void check_knob(int k, const char *name, int32_t max, int32_t def)
{
    const struct cm_knob *knob = &digistring_machine.ui->knob[k];
    assert(!strcmp(knob->name, name));
    assert(knob->look == 0x86);
    assert(knob->flags & CM_RANGE);
    assert(knob->flags & CM_DEFAULT);
    assert(knob->min == 0 && knob->max == V(max) && knob->def == V(def));
    assert(knob->fmt);
}

int main(void)
{
    const struct cm_ui *page = digistring_machine.ui;
    char out[16];

    /* The descriptor: machine 8 on SLICE's slots, with a page. */
    assert(digistring_machine.id == 8);
    assert(!strcmp(digistring_machine.name, "STRING"));
    assert(digistring_machine.params == 3 && digistring_machine.render == 8);
    assert(digistring_machine.tag == CM_UI_TAG);
    assert(page->abi == cm_ui_v31 && page->page_from == 3);
    assert(!strcmp(page->group, "STRG"));
    assert(!page->on_switch);

    /* A TUNE stock, D SAMP hidden, the rest STRING's own. */
    assert(!page->knob[0].name && !page->knob[0].flags);
    assert(page->knob[3].flags == CM_HIDDEN);
    check_knob(1, "EXC", 3, 3);
    assert(page->knob[1].flags & CM_DRAW && page->knob[1].u.draw);
    check_knob(2, "STIFF", 127, 0);
    check_knob(4, "POS", 64, 0);
    check_knob(5, "SOFT", 127, 0);
    check_knob(6, "TONE", 4, 0);
    check_knob(7, "DECAY", 127, 100);
    assert(!strcmp(page->knob[7].lname, "Decay"));

    /* Value texts. */
    assert(digistring_fmt_exc(out, V(3), 0, 8) && !strcmp(out, "PLUCK"));
    assert(digistring_fmt_exc(out, 0, 1, 8) && !strcmp(out, "BOW"));
    assert(digistring_fmt_pos(out, 0, 0, 8) && !strcmp(out, "OFF"));
    assert(digistring_fmt_pos(out, V(64), 0, 8) && !strcmp(out, "64"));
    assert(digistring_fmt_number(out, V(127), 0, 8) && !strcmp(out, "127"));
    assert(digistring_fmt_number(out, V(100), 1, 8) && !strcmp(out, "100"));
    assert(digistring_fmt_number(out, V(7), 0, 8) && !strcmp(out, "7"));
    assert(digistring_fmt_number(out, 0, 0, 8) && !strcmp(out, "0"));

    /* Small ranges span the 0..127 knob graphic. */
    assert(page->knob[4].u.gfx == digistring_gfx_pos);
    assert(page->knob[6].u.gfx == digistring_gfx_tone);
    assert(!page->knob[7].u.gfx);
    assert(digistring_gfx_pos(V(64)) == V(127));
    assert(digistring_gfx_pos(V(32)) == V(64));
    assert(digistring_gfx_tone(V(2)) == V(64));
    assert(digistring_gfx_tone(V(4)) == V(127));
    assert(digistring_gfx_tone(0) == 0);

    puts("ui: all tests passed");
    return 0;
}
