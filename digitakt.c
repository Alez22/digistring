/* SPDX-License-Identifier: MIT
 *
 * Digitakt Mk1 OS 1.53 and 1.54 adapter for the Karplus-Strong engine.
 *
 * The RAM addresses and the pitch lookup below were found by the digisophie
 * project (MIT) and are used the same way. mod.json's 1.54 port builds with
 * -DDIGISTRING_OS154: there the SRAM block (0x8000xxxx) is where it was,
 * while the RAM past the image and the pitch table moved.
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
#ifdef DIGISTRING_OS154
#define AMP_LEVEL_AT 0x4199ef58u
#define AMP_PHASE_AT 0x4199ef54u
#define PITCH_TAB_AT 0x4019b4c0u
#else
#define AMP_LEVEL_AT 0x4199df58u
#define AMP_PHASE_AT 0x4199df54u
#define PITCH_TAB_AT 0x4019b1c0u
#endif
#define AMP_LEVEL(t) (*(volatile const int32_t *)(unsigned long)(AMP_LEVEL_AT + 12u * (uint32_t)(t)))
#define AMP_PHASE(t) (*(volatile const int32_t *)(unsigned long)(AMP_PHASE_AT + 12u * (uint32_t)(t)))
#define PITCH_TAB ((const uint32_t *)(unsigned long)PITCH_TAB_AT)

/* SRC slots A..H (byte offsets into VP). The machine borrows SLICE's
 * parameters (core descriptor `params` = 3), so it keeps SLICE's ranges and
 * defaults; ui.c only renames and redraws them. Controls are placed so that
 * SLICE's defaults give a sensible new sound:
 *   A TUNE  stock              -> pitch
 *   B EXC   0..3,   def 3      -> BOW / HIT / NOISE / PLUCK (default PLUCK)
 *   C STIFF 0..127, def 0      -> dispersion (default: an ideal string)
 *   D SAMP                     -> unused, stock sample selector
 *   E POS   0..64,  def 0      -> pluck position comb (default: off)
 *   F SOFT  0..63,  def 0      -> darker excitation (default: bright)
 *   G TONE  0..4,   def 0      -> loop lowpass: 0 warm .. 3 bright, 4 open
 *   H DECAY 0..127, def 100    -> T60, 0.07 s .. ~17 s, then endless */
#define P_TUNE 0
#define P_EXC 2
#define P_STIFF 4
#define P_POS 8
#define P_SOFT 10
#define P_TONE 12
#define P_DECAY 14

/* 8 x ~4.1 KB, zeroed by core at boot: every voice starts inactive. */
static struct ks_voice ks_voices[TRACKS];

static uint32_t ks_u7(int32_t track, int32_t offset)
{ return ((uint32_t)(uint16_t)VP(track, offset) >> 8) & 0x7fu; }

/* String period at pitch ratio 1.0, in 48 kHz samples, Q8. Ratio 1.0 is
 * the stock playback rate of an untransposed sample, which the Digitakt
 * plays on note C4; STRING tunes it to 261.63 Hz: 48000 * 256 / 261.63. */
#define KS_C4_PERIOD_Q8 46968u
/* The stock pitch table covers 88 semitones; STRING extends it by
 * octaves on both sides instead of clamping. */
#define KS_TABLE_TOP (87 << 16)
#define KS_OCTAVE (12 << 16)

/**
 * @brief Pitch ratio (Q29) to a string period in 48 kHz samples, Q8.
 * period = KS_C4_PERIOD_Q8 * 2^29 / ratio. The divisor is normalized to
 * 16 bits first: one 32-bit divide per block, error below 0.1 cent.
 */
static uint32_t ks_period_q8(uint32_t ratio)
{
    uint32_t shift = 0;
    uint32_t inverse;
    if (ratio < (1u << 22)) ratio = 1u << 22; /* below the table: unused */
    while ((ratio >> shift) >= (1u << 16)) ++shift;
    /* inverse = 2^31 / (ratio >> shift), in [2^15, 2^16]; then
     * period = C4 * inverse * 2^(16 - shift) / 2^18. C4 * inverse stays
     * below 2^32, and shift >= 7 here. */
    inverse = (1u << 31) / (ratio >> shift);
    return (KS_C4_PERIOD_Q8 * inverse) >> (2u + shift);
}

/**
 * @brief The track's note and TUNE as a string period (Q8), using the
 * same stock pitch table as sample playback (digisophie reads it alike).
 * Pitches outside the table are moved into it by whole octaves and the
 * period is doubled or halved to match.
 */
static uint32_t ks_track_period_q8(int32_t track)
{
    int32_t pitch = ((int32_t)VP(track, P_TUNE) - 0x4000) * 256
        + NOTE(track) + (3 << 16);
    int32_t octaves = 0; /* > 0: higher than the table */
    uint32_t period;
    while (pitch < 0) { pitch += KS_OCTAVE; --octaves; }
    while (pitch > KS_TABLE_TOP) { pitch -= KS_OCTAVE; ++octaves; }
    period = ks_period_q8(PITCH_TAB[(uint32_t)pitch / 384u]);
    for (; octaves > 0; --octaves) period >>= 1;
    /* The engine stops at its ~1.5 Hz floor; stop doubling past it, so the
     * period cannot overflow. */
    for (; octaves < 0 && period <= KS_PERIOD_FLOOR_Q8; ++octaves) period <<= 1;
    return period;
}

static void ks_read_params(int32_t track, struct ks_params *p)
{
    /* EXC values in UI order, so SLICE's PLAY default (3) is PLUCK. */
    static const uint8_t exciters[4] = {
        KS_EXC_BOW, KS_EXC_MALLET, KS_EXC_NOISE, KS_EXC_PLUCK
    };
    uint32_t tone = ks_u7(track, P_TONE);
    uint32_t pos = ks_u7(track, P_POS) * 2u;
    uint32_t soft = ks_u7(track, P_SOFT);
    p->period_q8 = ks_track_period_q8(track);
    p->exciter = exciters[ks_u7(track, P_EXC) & 3u];
    p->stiff = (uint8_t)ks_u7(track, P_STIFF);
    p->pos = (uint8_t)(pos > 127u ? 127u : pos);
    p->bright = (uint8_t)(127u - (soft * 2u + (soft >= 63u)));
    p->tone = (uint8_t)(tone > 4u ? 4u : tone);
    p->decay = (uint8_t)ks_u7(track, P_DECAY);
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
 * buffers go to AMP/filter (site 0x40077fb2), once per 32-frame block.
 * Interrupt level: no firmware calls, static memory only.
 */
void digistring_inject(void)
{
    uint32_t triggers = TRIG_BITS;
    int32_t track;
    for (track = 0; track < TRACKS; ++track) {
        struct ks_voice *v = &ks_voices[track];
        struct ks_params params;
        int trigger;
        if (MACH(track) != KS_MACHINE) {
            digistring_voice_stop(v);
            continue;
        }
        trigger = (triggers & (1u << track)) != 0;
        digistring_voice_gate(v, AMP_LEVEL(track), AMP_PHASE(track));
        if (!trigger && (!v->active || v->sleeping)) {
            ks_zero_block(TBUF(track));
            continue;
        }
        ks_read_params(track, &params);
        digistring_voice_render(v, &params, trigger, TBUF(track), KS_BLOCK_SIZE);
    }
}
