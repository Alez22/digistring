| SPDX-License-Identifier: MIT
| STRING (machine 8): core 2.1 machine descriptor and the render hook.

| Firmware addresses that moved in OS 1.54 (mod.json's 1.54 port
| assembles with --defsym OS154=1). Every other address here, the sites'
| continuations included, is the same in both releases. Comments name
| the 1.53 addresses.
        .ifdef  OS154
        .equ    LAB_TABLE, 0x401aa09c       | lea'd by the label routines
        .equ    UI_REC_BASE, 0x4197e2f8     | the UI records
        .equ    POP_ARG, 0x4197de98         | the pop-up text's first argument
        .equ    LFO_LABEL_ARG, 0x401d0d7e   | pushed after a DEST name
        .equ    LFO_POP_ARG, 0x401d0405     | pushed after a DEST row's names
        .equ    LFO_POP_RET, 0x400a44dc
        .equ    LFO_POP_FB_RET, 0x400a4558
        .equ    LFO_OV_GROUP_FN, 0x4017af20 | called with the DEST box's group
        .equ    LFO_OV_NAME_ARG, 0x401c3de2 | pushed after the DEST box's name
        .equ    BMP_VT, 0x401b7734          | the stock Bitmap vtable
        .else
        .equ    LAB_TABLE, 0x401a9d9c
        .equ    UI_REC_BASE, 0x4197d2f8
        .equ    POP_ARG, 0x4197ce98
        .equ    LFO_LABEL_ARG, 0x401d09ca
        .equ    LFO_POP_ARG, 0x401d0051
        .equ    LFO_POP_RET, 0x400a4380
        .equ    LFO_POP_FB_RET, 0x400a43fc
        .equ    LFO_OV_GROUP_FN, 0x4017ac20
        .equ    LFO_OV_NAME_ARG, 0x401c3a62
        .equ    BMP_VT, 0x401b73b4
        .endif

        .section .run, "ax"

| Render hook at 0x40077fb2, replacing `addi.l #68,%d7` (6 bytes) with a
| jmp here. It runs right after the stock voice loop (0x400757fe) has
| written every voice's block and before the filter/AMP stages. The
| instructions after it are other mods' hooks (digisophie, digineighbor
| and digichain at 0x40077fba, digimono at 0x40077fc2, digihealth at
| 0x40077fc8), so this one keeps clear of all of them; NEIGHBOR, hooked
| later, can take a STRING track's sound. All registers are preserved:
| the stock render keeps live values in every one of them.
        .globl digistring_inject_s
digistring_inject_s:
        lea     -60(%sp), %sp
        movem.l %d0-%d7/%a0-%a6, (%sp)
        jsr     digistring_inject
        movem.l (%sp), %d0-%d7/%a0-%a6
        lea     60(%sp), %sp
        addi.l  #68, %d7                | the instruction this replaced
        jmp     0x40077fb8              | back to the stock render

| core_machines descriptor (elekloader docs/ADAPTING.md, "SRC machines"):
| id, name, short name, icon, params machine (3: SLICE's
| eight slots), render machine (its own id: an empty voice window).
        .balign 4
        .globl digistring_machine
digistring_machine:
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
        .globl digistring_layout_s
digistring_layout_s:
        move.l  4(%sp), %d0             | d0 is free at a call boundary
        move.l  %d0, digistring_page_m
        jmp     0x400657cc

| Short and long parameter names: 0x4000fe8a / 0x4000feac(?, param).
| Site: their `lea 0x401a9d9c,%a0` (jmp), with d1 = param and d0 the
| range test the stock code goes on with.
        .globl digistring_lab_short_s, digistring_lab_long_s
digistring_lab_short_s:
        lea     -8(%sp), %sp
        movem.l %d0-%d1, (%sp)
        clr.l   -(%sp)                  | short name
        move.l  %d1, -(%sp)
        jsr     digistring_ui_label
        addq.l  #8, %sp
        tst.l   %d0
        bne.s   1f
        movem.l (%sp), %d0-%d1
        lea     8(%sp), %sp
        lea     LAB_TABLE, %a0          | the instruction this replaced
        jmp     0x4000fe9c
1:      lea     8(%sp), %sp             | ours: the routine returns it
        rts
digistring_lab_long_s:
        lea     -8(%sp), %sp
        movem.l %d0-%d1, (%sp)
        pea     1                       | long name
        move.l  %d1, -(%sp)
        jsr     digistring_ui_label
        addq.l  #8, %sp
        tst.l   %d0
        bne.s   1f
        movem.l (%sp), %d0-%d1
        lea     8(%sp), %sp
        lea     LAB_TABLE, %a0          | the instruction this replaced
        jmp     0x4000febe
1:      lea     8(%sp), %sp
        rts

| UI record lookup 0x40065794(param): site its final `addi.l #base,%d0`
| (jmp), d0 = param index * 84. STRING's controls get BR's record.
        .globl digistring_ui_rec_s
digistring_ui_rec_s:
        lea     -8(%sp), %sp
        movem.l %d0-%d1, (%sp)
        move.l  12(%sp), -(%sp)         | the routine's param argument
        jsr     digistring_ui_record
        addq.l  #4, %sp
        tst.l   %d0
        bmi.s   1f
        moveq   #84, %d1
        muls.l  %d1, %d0
        lea     8(%sp), %sp
        bra.s   2f
1:      movem.l (%sp), %d0-%d1
        lea     8(%sp), %sp
2:      addi.l  #UI_REC_BASE, %d0       | the instruction this replaced
        rts

| Value text under a knob, 0x4000f324(obj, param, value, out): its call
| to the record lookup is redirected here (keep2). In its frame d4 =
| param, d2 = value, d3 = out. Ours: return from the whole routine with
| the text, as its own epilogue does; otherwise make the replaced call.
        .globl digistring_val_text_s
digistring_val_text_s:
        move.l  %d3, -(%sp)
        move.l  %d2, -(%sp)
        move.l  %d4, -(%sp)
        jsr     digistring_ui_text
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
        .globl digistring_knob_s
digistring_knob_s:
        move.l  %d5, -(%sp)
        move.l  %d4, -(%sp)
        move.l  %d3, -(%sp)
        move.l  %d2, -(%sp)
        move.l  20(%sp), -(%sp)         | the param being pushed for the call
        jsr     digistring_ui_draw
        lea     20(%sp), %sp
        tst.l   %d0
        bne.s   1f
        move.l  %d2, -(%sp)
        move.l  8(%sp), -(%sp)          | the param being pushed for the call
        jsr     digistring_ui_knob
        addq.l  #8, %sp
        move.l  %d0, %d2
        jmp     0x40065794              | the call this replaced
1:      addq.l  #8, %sp                 | our return address, its argument
        movem.l (%sp), %d2-%d6
        lea     20(%sp), %sp
        rts

| Popup value text, 0x400657ee(param, value): site its `pea 0x4197ce98`
| (jmp), with d0/d1 live for the stock code.
        .globl digistring_pop_text_s
digistring_pop_text_s:
        lea     -8(%sp), %sp
        movem.l %d0-%d1, (%sp)
        clr.l   -(%sp)                  | ui.c's own buffer
        move.l  20(%sp), -(%sp)         | value
        move.l  20(%sp), -(%sp)         | param
        jsr     digistring_ui_text
        lea     12(%sp), %sp
        tst.l   %d0
        bne.s   1f
        movem.l (%sp), %d0-%d1
        lea     8(%sp), %sp
        pea     POP_ARG                 | the instruction this replaced
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
        .globl digistring_lfo_label_s
digistring_lfo_label_s:
        lea     -16(%sp), %sp
        movem.l %d0-%d1/%a0-%a1, (%sp)
        clr.l   -(%sp)                  | short name
        move.l  %d0, -(%sp)
        jsr     digistring_ui_lfo_name
        addq.l  #8, %sp
        tst.l   %d0
        beq.s   1f
        move.l  %d0, 16(%sp)            | the name argument
1:      movem.l (%sp), %d0-%d1/%a0-%a1
        lea     16(%sp), %sp
        pea     LFO_LABEL_ARG           | the instruction this replaced
        jmp     0x40060bb0

| Destination popup rows "GROUP:Name": sites `pea 0x401d0051` at
| 0x400a437a (d0 = param * 52) and 0x400a43f6 (d2 = param * 52). (sp)
| holds the group, 4(sp) the long name. ks_lfo_pop does both slots.
| In: d1 = param * 52. Stack: our return, the 16 saved bytes, then the
| group (20(sp)) and the name (24(sp)).
ks_lfo_pop:
        move.l  %d1, -(%sp)
        jsr     digistring_ui_lfo_group_at
        move.l  (%sp)+, %d1
        tst.l   %d0
        beq.s   1f
        move.l  %d0, 4+16(%sp)          | the group argument
        pea     1                       | long name
        move.l  %d1, -(%sp)
        jsr     digistring_ui_lfo_name
        addq.l  #8, %sp
        tst.l   %d0
        beq.s   1f
        move.l  %d0, 4+16+4(%sp)        | the name argument
1:      rts
        .globl digistring_lfo_pop_s, digistring_lfo_pop_fb_s
digistring_lfo_pop_s:
        lea     -16(%sp), %sp
        movem.l %d0-%d1/%a0-%a1, (%sp)
        move.l  %d0, %d1
        bsr.s   ks_lfo_pop
        movem.l (%sp), %d0-%d1/%a0-%a1
        lea     16(%sp), %sp
        pea     LFO_POP_ARG             | the instruction this replaced
        jmp     LFO_POP_RET
digistring_lfo_pop_fb_s:
        lea     -16(%sp), %sp
        movem.l %d0-%d1/%a0-%a1, (%sp)
        move.l  %d2, %d1
        bsr.s   ks_lfo_pop
        movem.l (%sp), %d0-%d1/%a0-%a1
        lea     16(%sp), %sp
        pea     LFO_POP_ARG             | the instruction this replaced
        jmp     LFO_POP_FB_RET

| LFO overview, group line: its call `jsr 0x4017ac20` at 0x40065dec is
| redirected here (keep2). 8(sp) holds the group; d3 = param.
        .globl digistring_lfo_ov_group_s
digistring_lfo_ov_group_s:
        lea     -16(%sp), %sp
        movem.l %d0-%d1/%a0-%a1, (%sp)
        move.l  %d3, -(%sp)
        jsr     digistring_ui_lfo_group
        addq.l  #4, %sp
        tst.l   %d0
        beq.s   1f
        move.l  %d0, 16+8(%sp)          | the group argument
1:      movem.l (%sp), %d0-%d1/%a0-%a1
        lea     16(%sp), %sp
        jmp     LFO_OV_GROUP_FN         | the call this replaced

| LFO overview, name line: site `pea 0x401c3a62` at 0x40065e6e, the
| instruction after `pea 0x401d09ca`, which digimono hooks. 4(sp) holds
| the short name, (sp) 0x401d09ca; d3 = param * 52.
        .globl digistring_lfo_ov_name_s
digistring_lfo_ov_name_s:
        lea     -16(%sp), %sp
        movem.l %d0-%d1/%a0-%a1, (%sp)
        clr.l   -(%sp)                  | short name
        move.l  %d3, -(%sp)
        jsr     digistring_ui_lfo_name
        addq.l  #8, %sp
        tst.l   %d0
        beq.s   1f
        move.l  %d0, 16+4(%sp)          | the name argument
1:      movem.l (%sp), %d0-%d1/%a0-%a1
        lea     16(%sp), %sp
        pea     LFO_OV_NAME_ARG         | the instruction this replaced
        jmp     0x40065e74
