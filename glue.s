| SPDX-License-Identifier: MIT
| STRING (machine 8): core 2.1 machine descriptor and the render hook.
        .section .run, "ax"

| Render hook at 0x40077fc2, replacing `pea 0x80001a18` (6 bytes) with a
| jmp here. It sits after the stock source voices and before the buffers
| go to AMP/filter, and after digisophie's own hook at 0x40077fba, so both
| mods can be installed together. All registers are preserved: the stock
| render keeps live values in every one of them.
        .globl ks_inject_s
ks_inject_s:
        lea     -60(%sp), %sp
        movem.l %d0-%d7/%a0-%a6, (%sp)
        jsr     ks_inject
        movem.l (%sp), %d0-%d7/%a0-%a6
        lea     60(%sp), %sp
        pea     0x80001a18              | the instruction this replaced
        jmp     0x40077fc8              | back to the stock render

| core_machines descriptor (elekloader docs/ADAPTING.md, "SRC machines"):
| id, name, short name, icon, params machine (3: SLICE's
| eight slots), render machine (its own id: an empty voice window).
        .balign 4
        .globl ks_machine
ks_machine:
        .long   8, ks_name, ks_short, ks_icon, 3, 8
ks_name: .asciz "STRING"
ks_short: .asciz "STRG"

| Menu icon, in the firmware's Bitmap format as its blit (0x400c2960)
| reads it: vtable, width, height, longs per column, pixels, mask, 0.
| Pixels are stored by column, bit 31 first; bitmap y grows upwards, so
| bit 31 is the bottom row (this icon is symmetric, so it reads the same).
| The mask selects the pixels written. Drawn: a vibrating string between
| two end points.
|   ...........
|   ...#####...
|   .##.....##.
|   #.........#
|   .##.....##.
|   ...#####...
|   ...........
        .equ    BMP_VT, 0x401b73b4
        .balign 4
ks_icon:
        .long   BMP_VT, 11, 7, 1, ks_icon_px, ks_icon_mask, 0
ks_icon_px:
        .long   0x10000000, 0x28000000, 0x28000000, 0x44000000
        .long   0x44000000, 0x44000000, 0x44000000, 0x44000000
        .long   0x28000000, 0x28000000, 0x10000000
ks_icon_mask:                           | all 11 x 7 pixels opaque
        .long   0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000
        .long   0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000
        .long   0xfe000000, 0xfe000000, 0xfe000000

| ---- SRC page UI -------------------------------------------------------
| Each stub sits at an instruction after digisophie's entry hook in the
| same stock routine (or redirects a call into it), so with digisophie
| installed its hook runs first and falls through to ours, and without it
| the stock code reaches ours directly. ui.c decides; a zero (or -1)
| answer resumes the stock code exactly as it was.

| The SRC page layout routine 0x400657cc(machine) has two callers; both
| are redirected here (keep2) to remember the page's machine. digisophie
| hooks the routine's entry, so it still runs, after us.
        .globl ks_layout_s
ks_layout_s:
        move.l  4(%sp), %d0             | d0 is free at a call boundary
        move.l  %d0, ks_page_m
        jmp     0x400657cc

| Short and long parameter names: 0x4000fe8a / 0x4000feac(?, param).
| Site: their `lea 0x401a9d9c,%a0` (jmp), with d1 = param and d0 the
| range test the stock code goes on with.
        .globl ks_lab_short_s, ks_lab_long_s
ks_lab_short_s:
        lea     -8(%sp), %sp
        movem.l %d0-%d1, (%sp)
        clr.l   -(%sp)                  | short name
        move.l  %d1, -(%sp)
        jsr     ks_ui_label
        addq.l  #8, %sp
        tst.l   %d0
        bne.s   1f
        movem.l (%sp), %d0-%d1
        lea     8(%sp), %sp
        lea     0x401a9d9c, %a0         | the instruction this replaced
        jmp     0x4000fe9c
1:      lea     8(%sp), %sp             | ours: the routine returns it
        rts
ks_lab_long_s:
        lea     -8(%sp), %sp
        movem.l %d0-%d1, (%sp)
        pea     1                       | long name
        move.l  %d1, -(%sp)
        jsr     ks_ui_label
        addq.l  #8, %sp
        tst.l   %d0
        bne.s   1f
        movem.l (%sp), %d0-%d1
        lea     8(%sp), %sp
        lea     0x401a9d9c, %a0         | the instruction this replaced
        jmp     0x4000febe
1:      lea     8(%sp), %sp
        rts

| UI record lookup 0x40065794(param): site its final `addi.l #base,%d0`
| (jmp), d0 = param index * 84. STRING's controls get BR's record.
        .globl ks_ui_rec_s
ks_ui_rec_s:
        lea     -8(%sp), %sp
        movem.l %d0-%d1, (%sp)
        move.l  12(%sp), -(%sp)         | the routine's param argument
        jsr     ks_ui_record
        addq.l  #4, %sp
        tst.l   %d0
        bmi.s   1f
        moveq   #84, %d1
        muls.l  %d1, %d0
        lea     8(%sp), %sp
        bra.s   2f
1:      movem.l (%sp), %d0-%d1
        lea     8(%sp), %sp
2:      addi.l  #0x4197d2f8, %d0        | the instruction this replaced
        rts

| Value text under a knob, 0x4000f324(obj, param, value, out): its call
| to the record lookup is redirected here (keep2). In its frame d4 =
| param, d2 = value, d3 = out. Ours: return from the whole routine with
| the text, as its own epilogue does; otherwise make the replaced call.
        .globl ks_val_text_s
ks_val_text_s:
        move.l  %d3, -(%sp)
        move.l  %d2, -(%sp)
        move.l  %d4, -(%sp)
        jsr     ks_ui_text
        lea     12(%sp), %sp
        tst.l   %d0
        bne.s   1f
        jmp     0x40065794              | the call this replaced
1:      addq.l  #8, %sp                 | our return address, its argument
        movem.l (%sp), %d2-%d4/%a2-%a3
        lea     20(%sp), %sp
        rts

| Knob drawing, 0x4000f2bc(obj, param, value, flag, ?, screen, x, y):
| its call to the record lookup is redirected here (keep2). In its frame
| d2 = value (raw, 0..127 in the high byte), d3 = screen, d4 = x and
| d5 = y, as the page's knob routine (0x40030c0c) passes them. A control
| with an icon is drawn here and the whole routine returns, as its own
| epilogue does; otherwise scale the knob value and make the replaced call.
        .globl ks_knob_s
ks_knob_s:
        move.l  %d5, -(%sp)
        move.l  %d4, -(%sp)
        move.l  %d3, -(%sp)
        move.l  %d2, -(%sp)
        move.l  20(%sp), -(%sp)         | the param being pushed for the call
        jsr     ks_ui_draw
        lea     20(%sp), %sp
        tst.l   %d0
        bne.s   1f
        move.l  %d2, -(%sp)
        move.l  8(%sp), -(%sp)          | the param being pushed for the call
        jsr     ks_ui_knob
        addq.l  #8, %sp
        move.l  %d0, %d2
        jmp     0x40065794              | the call this replaced
1:      addq.l  #8, %sp                 | our return address, its argument
        movem.l (%sp), %d2-%d6
        lea     20(%sp), %sp
        rts

| Popup value text, 0x400657ee(param, value): site its `pea 0x4197ce98`
| (jmp), with d0/d1 live for the stock code.
        .globl ks_pop_text_s
ks_pop_text_s:
        lea     -8(%sp), %sp
        movem.l %d0-%d1, (%sp)
        clr.l   -(%sp)                  | ui.c's own buffer
        move.l  20(%sp), -(%sp)         | value
        move.l  20(%sp), -(%sp)         | param
        jsr     ks_ui_text
        lea     12(%sp), %sp
        tst.l   %d0
        bne.s   1f
        movem.l (%sp), %d0-%d1
        lea     8(%sp), %sp
        pea     0x4197ce98              | the instruction this replaced
        jmp     0x40065800
1:      lea     8(%sp), %sp
        rts

| ---- LFO destination names ---------------------------------------------
| digisophie replaces the instruction that loads each name; ours is the
| next 6-byte instruction, after the name is already on the stack. Ours
| swaps that stack slot for STRING's name, redoes the replaced
| instruction and resumes. On digisophie's page (machine 7) the stub sees
| it is not STRING's page and changes nothing. d0-d1/a0-a1 are kept: the
| stock code may still need them.

| LFO page DEST field: site `pea 0x401d09ca` at 0x40060baa. (sp) holds
| the short name; d0 = param * 52.
        .globl ks_lfo_label_s
ks_lfo_label_s:
        lea     -16(%sp), %sp
        movem.l %d0-%d1/%a0-%a1, (%sp)
        clr.l   -(%sp)                  | short name
        move.l  %d0, -(%sp)
        jsr     ks_ui_lfo_name
        addq.l  #8, %sp
        tst.l   %d0
        beq.s   1f
        move.l  %d0, 16(%sp)            | the name argument
1:      movem.l (%sp), %d0-%d1/%a0-%a1
        lea     16(%sp), %sp
        pea     0x401d09ca              | the instruction this replaced
        jmp     0x40060bb0

| Destination popup rows "GROUP:Name": sites `pea 0x401d0051` at
| 0x400a437a (d0 = param * 52) and 0x400a43f6 (d2 = param * 52). (sp)
| holds the group, 4(sp) the long name. ks_lfo_pop does both slots.
| In: d1 = param * 52. Stack: our return, the 16 saved bytes, then the
| group (20(sp)) and the name (24(sp)).
ks_lfo_pop:
        move.l  %d1, -(%sp)
        jsr     ks_ui_lfo_group_at
        move.l  (%sp)+, %d1
        tst.l   %d0
        beq.s   1f
        move.l  %d0, 4+16(%sp)          | the group argument
        pea     1                       | long name
        move.l  %d1, -(%sp)
        jsr     ks_ui_lfo_name
        addq.l  #8, %sp
        tst.l   %d0
        beq.s   1f
        move.l  %d0, 4+16+4(%sp)        | the name argument
1:      rts
        .globl ks_lfo_pop_s, ks_lfo_pop_fb_s
ks_lfo_pop_s:
        lea     -16(%sp), %sp
        movem.l %d0-%d1/%a0-%a1, (%sp)
        move.l  %d0, %d1
        bsr.s   ks_lfo_pop
        movem.l (%sp), %d0-%d1/%a0-%a1
        lea     16(%sp), %sp
        pea     0x401d0051              | the instruction this replaced
        jmp     0x400a4380
ks_lfo_pop_fb_s:
        lea     -16(%sp), %sp
        movem.l %d0-%d1/%a0-%a1, (%sp)
        move.l  %d2, %d1
        bsr.s   ks_lfo_pop
        movem.l (%sp), %d0-%d1/%a0-%a1
        lea     16(%sp), %sp
        pea     0x401d0051              | the instruction this replaced
        jmp     0x400a43fc

| LFO overview, group line: its call `jsr 0x4017ac20` at 0x40065dec is
| redirected here (keep2). 8(sp) holds the group; d3 = param.
        .globl ks_lfo_ov_group_s
ks_lfo_ov_group_s:
        lea     -16(%sp), %sp
        movem.l %d0-%d1/%a0-%a1, (%sp)
        move.l  %d3, -(%sp)
        jsr     ks_ui_lfo_group
        addq.l  #4, %sp
        tst.l   %d0
        beq.s   1f
        move.l  %d0, 16+8(%sp)          | the group argument
1:      movem.l (%sp), %d0-%d1/%a0-%a1
        lea     16(%sp), %sp
        jmp     0x4017ac20              | the call this replaced

| LFO overview, name line: site `pea 0x401d09ca` at 0x40065e68. (sp)
| holds the short name; d3 = param * 52.
        .globl ks_lfo_ov_name_s
ks_lfo_ov_name_s:
        lea     -16(%sp), %sp
        movem.l %d0-%d1/%a0-%a1, (%sp)
        clr.l   -(%sp)                  | short name
        move.l  %d3, -(%sp)
        jsr     ks_ui_lfo_name
        addq.l  #8, %sp
        tst.l   %d0
        beq.s   1f
        move.l  %d0, 16(%sp)            | the name argument
1:      movem.l (%sp), %d0-%d1/%a0-%a1
        lea     16(%sp), %sp
        pea     0x401d09ca              | the instruction this replaced
        jmp     0x40065e6e
