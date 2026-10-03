/* SPDX-License-Identifier: MIT
 *
 * Digitakt Mk1 OS 1.53 adapter for the Karplus-Strong engine.
 *
 * The RAM addresses and the pitch lookup below were found by the digisophie
 * project (MIT) and are used the same way; they are valid for OS 1.53 only.
 */
#include "karplus.h"

#define KS_MACHINE 8
#define TRACKS 8
/* Track source buffer: 32 samples, 16.16, read by the stock AMP/filter. */
#define TBUF(t) ((int32_t *)(unsigned long)(0x80001a18u + 128u * (uint32_t)(t)))
/* Each track's render machine, as core 2.1 writes it. */
#define MACH(t) (*(volatile const uint8_t *)(unsigned long)(0x800018bcu + (uint32_t)(t)))
/* Sound parameter block: one int16 per SRC slot, value in the high byte. */
#define VP(t, o) (*(volatile const int16_t *)(unsigned long)(0x80002794u + 106u * (uint32_t)(t) + (uint32_t)(o)))
#define NOTE(t) (*(volatile const int32_t *)(unsigned long)(0x80001f28u + 4u * (uint32_t)(t)))
#define VEL(t) (*(volatile const int16_t *)(unsigned long)(0x80001f18u + 2u * (uint32_t)(t)))
#define TRIG_BITS (*(volatile const uint32_t *)(unsigned long)0x80001228u)
#define AMP_LEVEL(t) (*(volatile const int32_t *)(unsigned long)(0x4199df58u + 12u * (uint32_t)(t)))
#define AMP_PHASE(t) (*(volatile const int32_t *)(unsigned long)(0x4199df54u + 12u * (uint32_t)(t)))
#define PITCH_TAB ((const uint32_t *)(unsigned long)0x4019b1c0u)

/* SRC slots A..H (byte offsets into VP). The machine borrows SLICE's
 * parameters (core descriptor `params` = 3), so until it has its own UI
 * the knobs show SLICE's names and ranges:
 *   A TUNE  stock       -> pitch
 *   B PLAY  0..3        -> EXC: NOISE / PLUCK / MALLET / BOW
 *   C BR    0..127      -> STIFF (default 0: an ideal string)
 *   D SAMP              -> unused, stock sample selector
 *   E SLICE 0..64       -> POS (default 0: comb off)
 *   F LEN   0..63       -> DECAY
 *   G GRID  0..4        -> DAMP in five steps
 *   H LEV   0..127      -> BRIGHT (default 100) */
#define P_TUNE 0
#define P_EXC 2
#define P_STIFF 4
#define P_POS 8
#define P_DECAY 10
#define P_DAMP 12
#define P_BRIGHT 14

/* 8 x ~2.1 KB, zeroed by core at boot: every voice starts inactive. */
static struct ks_voice ks_voices[TRACKS];

static uint32_t ks_u7(int32_t track, int32_t offset)
{ return ((uint32_t)(uint16_t)VP(track, offset) >> 8) & 0x7fu; }

/** Stock pitch ratio for the track's note and TUNE (Q29), as digisophie
 * reads it: the same table the stock sample playback uses. */
static uint32_t ks_pitch_ratio(int32_t track)
{
    int32_t pitch = ((int32_t)VP(track, P_TUNE) - 0x4000) * 256
        + NOTE(track) + (3 << 16);
    if (pitch < 0) pitch = 0;
    if (pitch > (87 << 16)) pitch = 87 << 16;
    return PITCH_TAB[(uint32_t)pitch / 384u];
}

/**
 * @brief Pitch ratio (Q29) to a string period in 48 kHz samples, Q8.
 * Ratio 1.0 plays 46.875 Hz (digisophie's calibration), so the period is
 * 2^39 / ratio samples, 2^47 / ratio in Q8. The divisor is normalized to
 * 16 bits first: one 32-bit divide per block, error below 0.1 cent.
 */
static uint32_t ks_period_q8(uint32_t ratio)
{
    uint32_t shift = 0;
    if (ratio < (1u << 22)) ratio = 1u << 22; /* below audio: folded anyway */
    while ((ratio >> shift) >= (1u << 16)) ++shift;
    /* shift >= 7 here, so the result stays below 2^25. */
    return ((1u << 31) / (ratio >> shift)) << (16u - shift);
}

static void ks_read_params(int32_t track, struct ks_params *p)
{
    static const uint8_t damp_steps[5] = { 0, 32, 64, 96, 127 };
    uint32_t grid = ks_u7(track, P_DAMP);
    uint32_t pos = ks_u7(track, P_POS) * 2u;
    uint32_t decay = ks_u7(track, P_DECAY);
    p->period_q8 = ks_period_q8(ks_pitch_ratio(track));
    p->exciter = (uint8_t)(ks_u7(track, P_EXC) & 3u);
    p->stiff = (uint8_t)ks_u7(track, P_STIFF);
    p->pos = (uint8_t)(pos > 127u ? 127u : pos);
    p->decay = (uint8_t)(decay * 2u + (decay >= 63u)); /* 0..127 */
    p->damp = damp_steps[grid > 4u ? 4u : grid];
    p->bright = (uint8_t)ks_u7(track, P_BRIGHT);
    p->velocity = (uint8_t)(((uint32_t)(uint16_t)VEL(track) >> 8) & 0x7fu);
}

static void ks_zero_block(int32_t *out)
{
    uint32_t i;
    for (i = 0; i < KS_BLOCK_SIZE; ++i) out[i] = 0;
}

/**
 * @brief Render every STRING track into its source buffer.
 * Called from the render after the stock source voices and before the
 * buffers go to AMP/filter (site 0x40077fc2), once per 32-frame block.
 * Interrupt level: no firmware calls, static memory only.
 */
void ks_inject(void)
{
    uint32_t triggers = TRIG_BITS;
    int32_t track;
    for (track = 0; track < TRACKS; ++track) {
        struct ks_voice *v = &ks_voices[track];
        struct ks_params params;
        int trigger;
        if (MACH(track) != KS_MACHINE) {
            ks_voice_stop(v);
            continue;
        }
        trigger = (triggers & (1u << track)) != 0;
        ks_voice_gate(v, AMP_LEVEL(track), AMP_PHASE(track));
        if (!trigger && (!v->active || v->sleeping)) {
            ks_zero_block(TBUF(track));
            continue;
        }
        ks_read_params(track, &params);
        ks_voice_render(v, &params, trigger, TBUF(track), KS_BLOCK_SIZE);
    }
}
