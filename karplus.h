/* SPDX-License-Identifier: MIT */
#ifndef DIGISTRING_KARPLUS_H
#define DIGISTRING_KARPLUS_H

#include <stdint.h>

#define KS_BLOCK_SIZE 32
/* Delay line length per voice. 1024 int16 samples = 2 KB per track, 16 KB
 * for eight tracks, all static. Kept a power of two for KS_LINE_MASK, and
 * this small so that STRING fits the shared 128 KB mod RAM next to large
 * mods such as digislicer. At 48 kHz the lowest full-rate note is ~47 Hz
 * (F#1); lower notes run the loop slower (see KS_PERIOD_FLOOR_Q8). */
#define KS_LINE_SIZE 1024u
#define KS_LINE_MASK (KS_LINE_SIZE - 1u)
/* Longest playable period, leaving room for the filters' own delay
 * (at most about 7 samples with STIFF at maximum). */
#define KS_PERIOD_MAX_Q8 (1016u << 8)
#define KS_PERIOD_MIN_Q8 (4u << 8)
/* Longer periods run the loop slower than the output (as Rings does)
 * instead of folding up an octave. Floor: 1/32 of the output rate, so the
 * lowest note is still about 1.5 Hz. */
#define KS_PERIOD_FLOOR_Q8 (KS_PERIOD_MAX_Q8 << 5)
#define KS_RATE_FULL 32768 /* loop steps per output sample, Q15 */

/* Excitation types, selected by the EXC control in four zones. */
enum ks_exciter {
    KS_EXC_NOISE = 0,  /* white noise burst: classic Karplus-Strong */
    KS_EXC_PLUCK = 1,  /* one triangle cycle: a softer, pitched pluck */
    KS_EXC_MALLET = 2, /* short triangle pulse: a struck string */
    KS_EXC_BOW = 3     /* low-level noise for as long as the note is held */
};

/* Controls already decoded by the Digitakt adapter. All u7 values are
 * 0..127 as stored in the SRC slots. */
struct ks_params {
    uint32_t period_q8; /* string period in 48 kHz samples, Q8 */
    uint8_t exciter;    /* enum ks_exciter */
    uint8_t bright;     /* excitation lowpass: 0 dark .. 127 open */
    uint8_t decay;      /* T60: 0 = 0.07 s .. 123 = ~17 s, 127 = infinite */
    uint8_t tone;       /* loop lowpass: 0 warm .. 3 bright, 4 open */
    uint8_t pos;        /* pluck position comb: 0 off, 1..127 bridge..middle */
    uint8_t stiff;      /* dispersion: 0 ideal string .. 127 stiff/bell */
    uint8_t velocity;   /* 0..127 */
};

struct ks_voice {
    int16_t line[KS_LINE_SIZE]; /* the string; Q15 */
    uint32_t write;             /* next write index into line */
    /* Output resampling for notes longer than the line (see KS_RATE_FULL):
     * the output crossfades from the previous to the current loop sample. */
    int32_t src_phase;          /* Q15, in (0, KS_RATE_FULL] */
    int32_t src_prev, src_cur;
    /* Loop filter states. */
    int32_t tone_z;                  /* TONE lowpass state */
    int32_t st1_x, st1_y, st2_x, st2_y; /* stiffness allpasses */
    int32_t fr_x, fr_y;              /* fractional tuning allpass */
    int32_t gain_rest;  /* DECAY's rounding remainder, carried (Q16) */
    /* Excitation, fixed at the trigger. */
    uint32_t exc_n;       /* loop steps since the trigger */
    uint32_t exc_len;     /* excitation stops here (BOW: while held) */
    uint32_t replace_left; /* loop steps that still ignore the stale line */
    uint32_t exc_period;  /* source period in whole loop steps */
    uint32_t exc_comb;    /* POS comb delay in samples, 0 = off */
    uint32_t exc_inc;     /* triangle phase step, 65536/period */
    int32_t exc_lp;       /* BRIGHT lowpass state */
    int32_t exc_gain;     /* velocity, Q15 */
    uint32_t rng_now, rng_comb; /* same noise sequence, comb-delayed copy */
    uint8_t exciter;
    /* Life cycle, as in Sophie: the stock AMP owns the note. */
    uint8_t active, sleeping, held;
    uint8_t quiet_blocks, silent_blocks;
    uint8_t fade_left;
};

void digistring_voice_init(struct ks_voice *voice);
void digistring_voice_stop(struct ks_voice *voice);
void digistring_voice_gate(struct ks_voice *voice, int32_t amp_level,
                   int32_t amp_phase);
void digistring_voice_render(struct ks_voice *voice, const struct ks_params *params,
                     int trigger, int32_t *output, uint32_t size);

#endif
