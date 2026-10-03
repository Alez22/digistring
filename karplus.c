/* SPDX-License-Identifier: MIT
 *
 * Fixed-point Karplus-Strong string for the Digitakt Mk1.
 *
 * The loop runs at the full 48 kHz output rate: it costs a handful of
 * multiplies per sample, and a lower internal rate would cost tuning and
 * range at the top of the keyboard.  The stock AMP, filter and effects
 * follow the source, as for any Digitakt machine.
 *
 * One loop sample:
 *   read line[write - taps] -> DAMP two-tap lowpass -> two STIFF allpasses
 *   -> fractional tuning allpass -> DECAY gain -> + excitation -> write
 * Total loop delay = taps + damp delay + stiffness delay + fraction, and it
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

/** Per-block loop coefficients, derived from the controls and the pitch. */
struct ks_loop {
    uint32_t taps;  /* integer delay-line length */
    int32_t damp;   /* DAMP mix of the previous sample, Q15, 0..0.5 */
    int32_t stiff;  /* STIFF allpass coefficient, Q15, 0..-0.45 */
    int32_t frac;   /* fractional tuning allpass coefficient, Q15 */
    int32_t gain;   /* DECAY loop gain, Q15, always below 1.0 */
};

static int32_t ks_clamp(int32_t x, int32_t lo, int32_t hi)
{ return x < lo ? lo : (x > hi ? hi : x); }

/** Exact 0..127 to 0..32767 without a divide: 32767 = 127*258 + 1. */
static int32_t ks_u7_q15(uint8_t x)
{ return ((int32_t)x << 8) + ((int32_t)x << 1) + (x == 127); }

static uint32_t ks_rand(uint32_t *state)
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
static int32_t ks_allpass(int32_t *x1, int32_t *y1, int32_t x, int32_t c)
{
    int32_t y = ((c * (x - *y1)) >> 15) + *x1;
    *x1 = x;
    *y1 = y;
    return y;
}

/**
 * @brief Fold a period into the delay line's range by whole octaves.
 * Notes below ~47 Hz play an octave (or more) higher instead of detuning.
 */
uint32_t ks_fold_period_q8(uint32_t period_q8)
{
    while (period_q8 > KS_PERIOD_MAX_Q8) period_q8 >>= 1;
    if (period_q8 < KS_PERIOD_MIN_Q8) period_q8 = KS_PERIOD_MIN_Q8;
    return period_q8;
}

/** DECAY 0..127 to a loop gain: quadratic so the top of the knob is fine. */
static int32_t ks_decay_gain(uint8_t decay)
{
    int32_t rest = 127 - (int32_t)decay;
    return Q15 - 1 - ((rest * rest) >> 2); /* 0.877 .. 0.99994 per pass */
}

/**
 * @brief Split the period between the integer delay and the filters.
 * The filters' delays are their DC phase delays, so the fundamental is in
 * tune while upper partials may drift, which is the STIFF control's point.
 */
static void ks_loop_setup(struct ks_loop *loop, const struct ks_params *p)
{
    int32_t period_q8 = (int32_t)ks_fold_period_q8(p->period_q8);
    int32_t filters_q8, rest_q8, taps;
    loop->damp = (int32_t)p->damp * 129; /* 0 .. 16383 = 0.5 */
    loop->stiff = -((int32_t)(p->stiff & 0x7fu) * 116);
    loop->gain = ks_decay_gain(p->decay);
    filters_q8 = (loop->damp >> 7)
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
 * That avoids clearing 2 KB inside the audio interrupt.
 */
static void ks_voice_trigger(struct ks_voice *v, const struct ks_params *p,
                             const struct ks_loop *loop)
{
    uint32_t period = ks_fold_period_q8(p->period_q8) >> 8;
    uint32_t source_length;
    if (!v->active || v->sleeping) {
        v->damp_z = 0;
        v->st1_x = v->st1_y = v->st2_x = v->st2_y = 0;
        v->fr_x = v->fr_y = 0;
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
static int32_t ks_triangle(uint32_t n, uint32_t inc)
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
static int32_t ks_source(const struct ks_voice *v, uint32_t n, uint32_t *rng)
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

static int ks_exciting(const struct ks_voice *v)
{
    if (v->exciter == KS_EXC_BOW) return v->held;
    return v->exc_n < v->exc_len;
}

/** Next excitation sample: source, POS comb, BRIGHT lowpass, velocity. */
static int32_t ks_excitation(struct ks_voice *v, uint8_t bright)
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

/** One trip of the string loop; returns the sample written to the line. */
static int32_t ks_loop_step(struct ks_voice *v, const struct ks_loop *loop,
                            int32_t excitation, int replacing)
{
    int32_t x = replacing ? 0 : v->line[(v->write - loop->taps) & KS_LINE_MASK];
    int32_t y = x + (((v->damp_z - x) * loop->damp) >> 15);
    v->damp_z = x;
    y = ks_allpass(&v->st1_x, &v->st1_y, y, loop->stiff);
    y = ks_allpass(&v->st2_x, &v->st2_y, y, loop->stiff);
    y = ks_allpass(&v->fr_x, &v->fr_y, y, loop->frac);
    /* Allpasses can overshoot their input peak; clamp before the gain so
     * the product stays in 32 bits. */
    y = ks_clamp(y, -32768, 32767);
    y = ((y * loop->gain) >> 15) + excitation;
    y = ks_clamp(y, -32768, 32767);
    v->line[v->write & KS_LINE_MASK] = (int16_t)y;
    ++v->write;
    return y;
}

void ks_voice_init(struct ks_voice *v)
{
    uint32_t i;
    for (i = 0; i < KS_LINE_SIZE; ++i) v->line[i] = 0;
    v->write = 0;
    v->damp_z = 0;
    v->st1_x = v->st1_y = v->st2_x = v->st2_y = 0;
    v->fr_x = v->fr_y = 0;
    v->exc_n = v->exc_len = v->exc_comb = v->exc_inc = 0;
    v->replace_left = v->exc_period = 0;
    v->exc_lp = v->exc_gain = 0;
    v->rng_now = v->rng_comb = 0x5354524eu;
    v->exciter = 0;
    v->active = v->sleeping = v->held = 0;
    v->quiet_blocks = v->silent_blocks = v->fade_left = 0;
}

/**
 * @brief Silence a voice without clearing its 2 KB line.
 * Cheap enough for the audio interrupt: the next trigger sees an inactive
 * voice and ignores the stale line for one trip round the loop.
 */
void ks_voice_stop(struct ks_voice *v)
{
    v->active = 0;
}

/* Stock AMP owns note-off and duration, exactly as in Sophie. Its level
 * passes through zero at the start of an envelope, so only phase zero (the
 * released/idle state) with a low level counts as quiet. Unlike Sophie, a
 * sleeping string is only woken by a trigger: its energy is gone anyway. */
void ks_voice_gate(struct ks_voice *v, int32_t amp_level, int32_t amp_phase)
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

void ks_voice_render(struct ks_voice *v, const struct ks_params *p,
                     int trigger, int32_t *out, uint32_t n)
{
    struct ks_loop loop;
    uint32_t i;
    int32_t peak = 0;
    ks_loop_setup(&loop, p);
    if (trigger) ks_voice_trigger(v, p, &loop);
    if (!v->active || v->sleeping) {
        ks_zero(out, n);
        return;
    }
    for (i = 0; i < n; ++i) {
        int replacing = v->replace_left != 0;
        int32_t excitation = ks_excitation(v, p->bright);
        int32_t y = ks_loop_step(v, &loop, excitation, replacing);
        int32_t mag = y < 0 ? -y : y;
        if (replacing) --v->replace_left;
        if (mag > peak) peak = mag;
        /* A full-scale string reaches 0.25 FS, Sophie's source level. */
        y >>= 2;
        if (v->fade_left) {
            y = (y * v->fade_left) >> 7;
            if (--v->fade_left == 0) {
                v->sleeping = 1;
                ks_zero(out + i + 1, n - i - 1);
                out[i] = 0;
                return;
            }
        }
        out[i] = y * 65536;
    }
    ks_track_silence(v, peak);
}
