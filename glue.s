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
| id, name, short name, icon (0: none yet), params machine (3: SLICE's
| eight slots), render machine (its own id: an empty voice window).
        .balign 4
        .globl ks_machine
ks_machine:
        .long   8, ks_name, ks_short, 0, 3, 8
ks_name: .asciz "STRING"
ks_short: .asciz "STRG"
