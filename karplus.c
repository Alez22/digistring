/* SPDX-License-Identifier: MIT
 *
 * Fixed-point Karplus-Strong string for the Digitakt Mk1.
 *
 * The loop runs at the full 48 kHz output rate: it costs a handful of
 * multiplies per sample, and a lower internal rate would cost tuning and
 * range at the top of the keyboard. Only notes too long for the delay line
 * run it slower, with linear interpolation on the output. The stock AMP,
 * filter and effects follow the source, as for any Digitakt machine.
 *
 * DECAY as a T60, TONE's cutoff tracking the note and the decay, and the
 * slow loop for very low notes follow the String model of Mutable
 * Instruments Rings (Emilie Gillet, MIT).
 *
 * One loop sample:
 *   read line[write - taps] -> TONE one-pole lowpass -> two STIFF allpasses
 *   -> fractional tuning allpass -> DECAY gain -> + excitation -> write
 * Total loop delay = taps + tone delay + stiffness delay + fraction, and it
 * equals the requested period at low frequency.
 */
#include "karplus.h"

#define Q15 32767
/* Loop peak below which a voice counts as silent (about -54 dBFS). */
#define KS_SILENT_PEAK 64
/* Blocks of silence or of a quiet released AMP before the voice fades. */
#define KS_QUIET_BLOCKS 32
/* Fade-out before sleep: 128 samples, about 2.7 ms at 48 kHz. */
#define KS_FADE_SAMPLES 128

#include "ks_tables.inc"

/* The per-sample path lives in ks_loop.inc. The STRING-fast build compiles
 * it a second time into .fast, which FAST AUDIO copies to on-chip SRAM; its
 * helpers are forced inline so the SRAM copy never calls back out. */
#define KS_HOT static inline __attribute__((always_inline))

/** Per-block loop coefficients, derived from the controls and the pitch. */
struct ks_loop {
    uint32_t taps;  /* integer delay-line length */
    int32_t tone;   /* TONE lowpass coefficient, Q15; 0 = no filter */
    int32_t stiff;  /* STIFF allpass coefficient, Q15, 0..-0.45 */
    int32_t frac;   /* fractional tuning allpass coefficient, Q15 */
    int32_t gain;   /* DECAY loop gain, Q16, always below 1.0 */
    uint32_t period_q8; /* loop period in loop steps, Q8 */
    int32_t rate;   /* loop steps per output sample, Q15 */
};

KS_HOT int32_t ks_clamp(int32_t x, int32_t lo, int32_t hi)
{ return x < lo ? lo : (x > hi ? hi : x); }

/** Exact 0..127 to 0..32767 without a divide: 32767 = 127*258 + 1. */
static int32_t ks_u7_q15(uint8_t x)
{ return ((int32_t)x << 8) + ((int32_t)x << 1) + (x == 127); }

KS_HOT uint32_t ks_rand(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *state = x;
    return x;
}

/**
 * @brief First-order allpass, y = c*(x - y1) + x1.
 * @param x1 previous input, updated.
 * @param y1 previous output, updated.
 * Inputs stay within int16 plus allpass overshoot, so c*(x - y1) fits in
 * 32 bits for |c| <= 0.45.
 */
KS_HOT int32_t ks_allpass(int32_t *x1, int32_t *y1, int32_t x, int32_t c)
{
    int32_t y = ((c * (x - *y1)) >> 15) + *x1;
    *x1 = x;
    *y1 = y;
    return y;
}

/** The played period, kept between the shortest one and the 1.5 Hz floor. */
static uint32_t ks_limit_period(uint32_t period_q8)
{
    if (period_q8 < KS_PERIOD_MIN_Q8) return KS_PERIOD_MIN_Q8;
    if (period_q8 > KS_PERIOD_FLOOR_Q8) return KS_PERIOD_FLOOR_Q8;
    return period_q8;
}

/** 2^-e for e >= 0 in Q16, from a 65-point table; 0 below 2^-16. */
static uint32_t ks_exp2_neg(uint32_t e_q16)
{
    uint32_t octaves = e_q16 >> 16;
    uint32_t index = (e_q16 >> 10) & 63u;
    uint32_t fraction = e_q16 & 1023u;
    uint32_t value;
    if (octaves >= 16) return 0;
    value = ks_exp2_neg_q16[index]
        - (((ks_exp2_neg_q16[index] - ks_exp2_neg_q16[index + 1]) * fraction)
           >> 10);
    return value >> octaves;
}

/**
 * @brief Loop gain (Q16) for a T60 that does not depend on the note.
 * Each trip round the loop lasts one period, so -60 dB after T60 needs
 * gain = 2^(-10 * period / T60) per trip (2^-10 is about -60 dB).
 * @param period_q8 the played period in output samples (at most the floor).
 */
static int32_t ks_decay_gain(uint8_t decay, uint32_t period_q8)
{
    uint32_t t60 = ks_decay_rt60[decay & 0x7fu];
    /* exponent = 10 * period / T60, Q16. period * 10 < 2^27 here, so it is
     * shifted by 5 and T60 (>= 3360) by 3 to keep 32 bits: one divide. */
    uint32_t gain = ks_exp2_neg(((period_q8 * 10u) << 5) / (t60 >> 3));
    if (gain > 65535u) gain = 65535u;
    /* The top of the knob crossfades to an endless string. */
    if (decay > 120)
        gain += ((65535u - gain) * ks_decay_infinite_q8[decay - 121]) >> 8;
    return (int32_t)gain;
}

/* TONE 0..3: the lowpass cutoff in semitones above the note before DECAY
 * adds its share; Rings' 24 + brightness^2 * 24 in four steps. TONE 4 has
 * no lowpass at all. */
static const uint8_t ks_tone_base[4] = { 24, 30, 38, 48 };
#define KS_TONE_OPEN 4
/* Rings caps the cutoff 7 octaves above the note; past 0.45 of the loop
 * rate the filter is left out, as it barely acts there. */
#define KS_CUTOFF_MAX_Q8 (84 << 8)
#define KS_CUTOFF_OPEN_Q8 (128 << 8)
#define KS_X_OPEN_Q16 29491

/** Cutoff in semitones above the note (Q8), as Rings sets it. */
static uint32_t ks_tone_semitones(uint8_t tone, uint8_t decay)
{
    uint32_t s = ((uint32_t)ks_tone_base[tone] << 8)
        + ks_decay_cutoff_q8[decay & 0x7fu];
    if (s > KS_CUTOFF_MAX_Q8) s = KS_CUTOFF_MAX_Q8;
    /* An endless string needs an open filter too. */
    if (decay > 120)
        s += ((KS_CUTOFF_OPEN_Q8 - s) * ks_decay_infinite_q8[decay - 121]) >> 8;
    return s;
}

/**
 * @brief TONE's lowpass coefficient (Q15, 0 = no filter) and its delay.
 * The cutoff sits a fixed interval above the note, so a TONE setting
 * sounds alike across the keyboard. The filter's delay at the note is
 * taken as its DC group delay, (1 - k) / k samples: the cutoff is at least
 * two octaves above the note, so the fundamental stays within ~1.5 cents.
 * @param period_q8 the loop period, in loop steps.
 * @param delay_q8 out: the filter's delay in loop steps, Q8.
 */
static int32_t ks_tone_setup(uint8_t tone, uint8_t decay, uint32_t period_q8,
                             int32_t *delay_q8)
{
    uint32_t twelfths, ratio_q16, x_q16, index, fraction;
    int32_t k;
    *delay_q8 = 0;
    if (tone >= KS_TONE_OPEN) return 0;
    /* ratio = 2^(semitones / 12), Q16; up to 2^(128/12) < 2^11. */
    twelfths = (ks_tone_semitones(tone, decay) << 8) / 12u;
    ratio_q16 = (twelfths & 0xffffu)
        ? ks_exp2_neg(65536u - (twelfths & 0xffffu)) << ((twelfths >> 16) + 1)
        : 65536u << (twelfths >> 16);
    /* x = cutoff / loop rate = ratio / period, Q16. ratio < 2^27, so
     * ratio << 4 fits; period >> 4 keeps 6+ bits, enough for a cutoff. */
    x_q16 = (ratio_q16 << 4) / (period_q8 >> 4);
    if (x_q16 >= KS_X_OPEN_Q16) return 0;
    index = x_q16 >> 8;
    fraction = x_q16 & 255u;
    k = (int32_t)ks_onepole_k[index]
        + ((((int32_t)ks_onepole_k[index + 1] - (int32_t)ks_onepole_k[index])
            * (int32_t)fraction) >> 8);
    if (k < 1) k = 1;
    *delay_q8 = ((32768 - k) << 8) / k;
    return k;
}

/**
 * @brief Loop speed for a period: full rate while the string fits in the
 * line, slower below that. The loop then has a shorter period of its own.
 */
static void ks_loop_rate(struct ks_loop *loop, uint32_t period_q8)
{
    if (period_q8 <= KS_PERIOD_MAX_Q8) {
        loop->rate = KS_RATE_FULL;
        loop->period_q8 = period_q8;
        return;
    }
    /* rate = MAX / period, Q15: MAX << 12 fits 32 bits and period >> 4
     * keeps 14+ bits, so the pitch error stays below 0.5 cent. */
    loop->rate = (int32_t)(((KS_PERIOD_MAX_Q8 << 12) / (period_q8 >> 4)) >> 1);
    loop->period_q8 = ((period_q8 >> 7) * (uint32_t)loop->rate) >> 8;
}

/**
 * @brief Split the period between the integer delay and the filters.
 * The filters' delays are their DC phase delays, so the fundamental is in
 * tune while upper partials may drift, which is the STIFF control's point.
 */
static void ks_loop_setup(struct ks_loop *loop, const struct ks_params *p)
{
    uint32_t played_q8 = ks_limit_period(p->period_q8);
    int32_t period_q8, filters_q8, rest_q8, taps, tone_delay_q8;
    ks_loop_rate(loop, played_q8);
    period_q8 = (int32_t)loop->period_q8;
    loop->tone = ks_tone_setup(p->tone, p->decay, loop->period_q8,
                               &tone_delay_q8);
    loop->stiff = -((int32_t)(p->stiff & 0x7fu) * 116);
    loop->gain = ks_decay_gain(p->decay, played_q8);
    filters_q8 = tone_delay_q8
        + 2 * (int32_t)ks_stiff_delay_q8[p->stiff & 0x7fu];
    rest_q8 = period_q8 - filters_q8;
    /* Keep at least two whole taps plus a 0.5 fraction; very high notes
     * with heavy STIFF then go slightly flat instead of breaking. */
    if (rest_q8 < (2 << 8) + 128) rest_q8 = (2 << 8) + 128;
    /* The fraction stays in [0.5, 1.5), the allpass's well-behaved range. */
    taps = (rest_q8 - 128) >> 8;
    loop->taps = (uint32_t)taps;
    loop->frac = ks_frac_coef[rest_q8 - (taps << 8) - 128];
}

/** Excitation length before the POS comb, in samples. */
static uint32_t ks_source_length(uint8_t exciter, uint32_t period)
{
    switch (exciter) {
    case KS_EXC_MALLET: return period / 4 + 1;
    case KS_EXC_BOW: return 0xffffffffu; /* stops on release instead */
    default: return period;
    }
}

/**
 * @brief Start a new excitation.
 * A string that still rings is plucked again: the excitation adds to it.
 * A silent or sleeping string has stale samples in its line, so the first
 * `taps` samples (one trip round the loop) ignore what the line returns.
 * That avoids clearing 4 KB inside the audio interrupt.
 */
static void ks_voice_trigger(struct ks_voice *v, const struct ks_params *p,
                             const struct ks_loop *loop)
{
    uint32_t period = loop->period_q8 >> 8;
    uint32_t source_length;
    if (!v->active || v->sleeping) {
        v->tone_z = 0;
        v->st1_x = v->st1_y = v->st2_x = v->st2_y = 0;
        v->fr_x = v->fr_y = 0;
        v->gain_rest = 0;
        v->src_phase = KS_RATE_FULL;
        v->src_prev = v->src_cur = 0;
        v->replace_left = loop->taps;
    } else {
        v->replace_left = 0;
    }
    v->exciter = p->exciter & 3u;
    v->exc_n = 0;
    v->exc_comb = p->pos ? (period * p->pos) >> 8 : 0; /* up to half */
    v->exc_period = period;
    v->exc_inc = 65536u / period; /* one divide per trigger */
    source_length = ks_source_length(v->exciter, period);
    v->exc_len = v->exciter == KS_EXC_BOW ? source_length
                                          : source_length + v->exc_comb;
    v->exc_lp = 0;
    v->exc_gain = ks_u7_q15(p->velocity);
    v->rng_now ^= 0x9e3779b9u + ((uint32_t)p->velocity << 16);
    if (!v->rng_now) v->rng_now = 0x5354524eu;
    v->rng_comb = v->rng_now;
    v->active = 1;
    v->held = 1;
    v->sleeping = v->quiet_blocks = v->silent_blocks = v->fade_left = 0;
}

/** One triangle cycle over `inc` steps per sample, +/-16384, zero mean. */
KS_HOT int32_t ks_triangle(uint32_t n, uint32_t inc)
{
    uint32_t phase = (n * inc) & 0xffffu;
    int32_t up = (int32_t)(phase < 32768u ? phase : 65535u - phase);
    return up - 16384;
}

/**
 * @brief Raw excitation source at sample n after the trigger.
 * @param rng the noise state for this read: the comb tap has its own copy
 *            of the sequence, so it hears exactly the delayed source.
 */
KS_HOT int32_t ks_source(const struct ks_voice *v, uint32_t n, uint32_t *rng)
{
    switch (v->exciter) {
    case KS_EXC_PLUCK:
        return n < v->exc_period ? ks_triangle(n, v->exc_inc) : 0;
    case KS_EXC_MALLET:
        return n < v->exc_period / 4 ? ks_triangle(n, v->exc_inc * 4u) : 0;
    case KS_EXC_BOW:
        return ((int32_t)(ks_rand(rng) >> 16) - 32768) >> 3;
    default:
        return n < v->exc_period
            ? ((int32_t)(ks_rand(rng) >> 16) - 32768) >> 1 : 0;
    }
}

KS_HOT int ks_exciting(const struct ks_voice *v)
{
    if (v->exciter == KS_EXC_BOW) return v->held;
    return v->exc_n < v->exc_len;
}

/** Next excitation sample: source, POS comb, BRIGHT lowpass, velocity. */
KS_HOT int32_t ks_excitation(struct ks_voice *v, uint8_t bright)
{
    int32_t source;
    int32_t k = 1024 + (int32_t)bright * 243; /* keeps (e - lp)*k in 32 bits */
    if (!ks_exciting(v)) return 0;
    source = ks_source(v, v->exc_n, &v->rng_now);
    if (v->exc_comb && v->exc_n >= v->exc_comb)
        source -= ks_source(v, v->exc_n - v->exc_comb, &v->rng_comb);
    v->exc_lp += ((source - v->exc_lp) * k) >> 15;
    ++v->exc_n;
    return (v->exc_lp * v->exc_gain) >> 15;
}

/**
 * @brief y * gain (Q16) with the rounding remainder carried to the next
 * sample (first-order error feedback).
 * Plain truncation of an int16 string loses a whole LSB per trip on
 * positive samples and none on negative ones: that bias would swamp the
 * Q16 gain of long decays and push the string towards a negative offset.
 * Carrying the remainder makes the average gain exact for both signs, so
 * the string decays as asked, down to zero.
 * |y| <= 2^15 and gain < 2^16, so y * gain + rest fits 32 bits.
 */
KS_HOT int32_t ks_apply_gain(struct ks_voice *v, int32_t y, int32_t gain)
{
    int32_t scaled = y * gain + v->gain_rest;
    v->gain_rest = (int32_t)((uint32_t)scaled & 0xffffu);
    return scaled >> 16;
}

/** One trip of the string loop; returns the sample written to the line. */
KS_HOT int32_t ks_loop_step(struct ks_voice *v, const struct ks_loop *loop,
                            int32_t excitation, int replacing)
{
    int32_t y = replacing ? 0 : v->line[(v->write - loop->taps) & KS_LINE_MASK];
    if (loop->tone) {
        /* |y - z| <= 2^16 and tone <= 2^15: fits 32 bits. */
        v->tone_z += ((y - v->tone_z) * loop->tone) >> 15;
        y = v->tone_z;
    }
    y = ks_allpass(&v->st1_x, &v->st1_y, y, loop->stiff);
    y = ks_allpass(&v->st2_x, &v->st2_y, y, loop->stiff);
    y = ks_allpass(&v->fr_x, &v->fr_y, y, loop->frac);
    /* Allpasses can overshoot their input peak; clamp before the gain so
     * the product stays in 32 bits. */
    y = ks_clamp(y, -32768, 32767);
    y = ks_apply_gain(v, y, loop->gain) + excitation;
    y = ks_clamp(y, -32768, 32767);
    v->line[v->write & KS_LINE_MASK] = (int16_t)y;
    ++v->write;
    return y;
}

void digistring_voice_init(struct ks_voice *v)
{
    uint32_t i;
    for (i = 0; i < KS_LINE_SIZE; ++i) v->line[i] = 0;
    v->write = 0;
    v->src_phase = KS_RATE_FULL;
    v->src_prev = v->src_cur = 0;
    v->tone_z = 0;
    v->st1_x = v->st1_y = v->st2_x = v->st2_y = 0;
    v->fr_x = v->fr_y = 0;
    v->gain_rest = 0;
    v->exc_n = v->exc_len = v->exc_comb = v->exc_inc = 0;
    v->replace_left = v->exc_period = 0;
    v->exc_lp = v->exc_gain = 0;
    v->rng_now = v->rng_comb = 0x5354524eu;
    v->exciter = 0;
    v->active = v->sleeping = v->held = 0;
    v->quiet_blocks = v->silent_blocks = v->fade_left = 0;
}

/**
 * @brief Silence a voice without clearing its 4 KB line.
 * Cheap enough for the audio interrupt: the next trigger sees an inactive
 * voice and ignores the stale line for one trip round the loop.
 */
void digistring_voice_stop(struct ks_voice *v)
{
    v->active = 0;
}

/* Stock AMP owns note-off and duration, exactly as in Sophie. Its level
 * passes through zero at the start of an envelope, so only phase zero (the
 * released/idle state) with a low level counts as quiet. Unlike Sophie, a
 * sleeping string is only woken by a trigger: its energy is gone anyway. */
void digistring_voice_gate(struct ks_voice *v, int32_t amp_level, int32_t amp_phase)
{
    uint32_t mag;
    if (!v->active) return;
    v->held = amp_phase != 0;
    mag = amp_level < 0 ? 0u - (uint32_t)amp_level : (uint32_t)amp_level;
    if (amp_phase != 0 || mag > (1u << 19)) {
        v->quiet_blocks = 0;
        return;
    }
    if (v->quiet_blocks < KS_QUIET_BLOCKS
        && ++v->quiet_blocks == KS_QUIET_BLOCKS && !v->fade_left)
        v->fade_left = KS_FADE_SAMPLES;
}

/** Count silent blocks; a decayed string fades and sleeps even if held. */
static void ks_track_silence(struct ks_voice *v, int32_t peak)
{
    if (peak >= KS_SILENT_PEAK || ks_exciting(v)) {
        v->silent_blocks = 0;
        return;
    }
    if (v->silent_blocks < KS_QUIET_BLOCKS
        && ++v->silent_blocks == KS_QUIET_BLOCKS && !v->fade_left)
        v->fade_left = KS_FADE_SAMPLES;
}

static void ks_zero(int32_t *out, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; ++i) out[i] = 0;
}

/**
 * @brief One loop step: excitation, string, peak and the replace window.
 * @return the new loop sample.
 */
KS_HOT int32_t ks_step(struct ks_voice *v, const struct ks_loop *loop,
                       uint8_t bright, int32_t *peak)
{
    int replacing = v->replace_left != 0;
    int32_t excitation = ks_excitation(v, bright);
    int32_t y = ks_loop_step(v, loop, excitation, replacing);
    int32_t mag = y < 0 ? -y : y;
    if (replacing) --v->replace_left;
    if (mag > *peak) *peak = mag;
    return y;
}

#define KS_LOOP_NAME ks_render_samples
#define KS_LOOP_SECTION ".text"
#include "ks_loop.inc"
#undef KS_LOOP_NAME
#undef KS_LOOP_SECTION
#ifdef KS_FAST
/* The STRING-fast build (variants/fast) adds a second copy of the sample
 * loop in .fast, which digihealth's FAST AUDIO copies to on-chip SRAM. A
 * .fast section needs digihealth installed, so the normal build leaves it
 * out entirely. */
#define KS_LOOP_NAME ks_render_samples_fast
#define KS_LOOP_SECTION ".fast"
#include "ks_loop.inc"
#undef KS_LOOP_NAME
#undef KS_LOOP_SECTION

/* digihealth's FAST AUDIO flag: 1 once its SRAM copies, ours included,
 * are made, checked and in use; 0 when switched off or when its watchdog
 * finds them overwritten. Weak: without digihealth it reads as absent. */
extern volatile uint32_t r_on __attribute__((weak));

/** True when the .fast copy of the sample loop is in SRAM and valid. */
static int ks_fast_ready(void)
{
    return &r_on != 0 && r_on != 0;
}
#else
static int ks_fast_ready(void) { return 0; }
#define ks_render_samples_fast ks_render_samples
#endif

void digistring_voice_render(struct ks_voice *v, const struct ks_params *p,
                     int trigger, int32_t *out, uint32_t n)
{
    struct ks_loop loop;
    uint32_t done;
    int32_t peak = 0;
    ks_loop_setup(&loop, p);
    if (trigger) ks_voice_trigger(v, p, &loop);
    if (!v->active || v->sleeping) {
        ks_zero(out, n);
        return;
    }
    done = ks_fast_ready()
        ? ks_render_samples_fast(v, &loop, p->bright, out, n, &peak)
        : ks_render_samples(v, &loop, p->bright, out, n, &peak);
    if (done < n) {
        /* The fade ended and the voice went to sleep mid-block. */
        ks_zero(out + done, n - done);
        return;
    }
    ks_track_silence(v, peak);
}
