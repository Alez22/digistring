/* SPDX-License-Identifier: MIT */
#ifndef DIGISTRING_KARPLUS_H
#define DIGISTRING_KARPLUS_H

#include <stdint.h>

#define KS_BLOCK_SIZE 32
/* Delay line length per voice. 1024 int16 samples = 2 KB per track, 16 KB
 * for eight tracks, all static. At 48 kHz the lowest note is ~47 Hz;
 * lower notes are folded up by octaves (see ks_fold_period_q8). */
#define KS_LINE_SIZE 1024u
#define KS_LINE_MASK (KS_LINE_SIZE - 1u)
/* Longest playable period, leaving room for the filters' own delay. */
#define KS_PERIOD_MAX_Q8 (1000u << 8)
#define KS_PERIOD_MIN_Q8 (4u << 8)

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
    uint8_t decay;      /* loop gain: 0 short .. 127 nearly infinite */
    uint8_t damp;       /* loop lowpass: 0 metallic .. 127 classic KS */
    uint8_t pos;        /* pluck position comb: 0 off, 1..127 bridge..middle */
    uint8_t stiff;      /* dispersion: 0 ideal string .. 127 stiff/bell */
    uint8_t velocity;   /* 0..127 */
};

struct ks_voice {
    int16_t line[KS_LINE_SIZE]; /* the string; Q15 */
    uint32_t write;             /* next write index into line */
    /* Loop filter states. */
    int32_t damp_z;                  /* previous delay-line read */
    int32_t st1_x, st1_y, st2_x, st2_y; /* stiffness allpasses */
    int32_t fr_x, fr_y;              /* fractional tuning allpass */
    /* Excitation, fixed at the trigger. */
    uint32_t exc_n;       /* samples since the trigger */
    uint32_t exc_len;     /* excitation stops here (BOW: while held) */
    uint32_t replace_left; /* samples that still ignore the stale line */
    uint32_t exc_period;  /* source period in whole samples */
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

void ks_voice_init(struct ks_voice *voice);
void ks_voice_stop(struct ks_voice *voice);
void ks_voice_gate(struct ks_voice *voice, int32_t amp_level,
                   int32_t amp_phase);
void ks_voice_render(struct ks_voice *voice, const struct ks_params *params,
                     int trigger, int32_t *output, uint32_t size);
uint32_t ks_fold_period_q8(uint32_t period_q8);

#endif
