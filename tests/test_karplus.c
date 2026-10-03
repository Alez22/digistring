/* SPDX-License-Identifier: MIT */
/* Host tests for the Karplus-Strong engine: tuning, decay, life cycle. */
#define _DEFAULT_SOURCE /* M_PI */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../karplus.h"

#define RATE 48000
#define N RATE

static int32_t buf_a[N], buf_b[N];

/** Render `n` samples in Digitakt blocks; AMP held, or released if !held. */
static void render(struct ks_voice *v, const struct ks_params *p, int trigger,
                   int held, int32_t *out, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i += KS_BLOCK_SIZE) {
        ks_voice_gate(v, held ? 1 << 24 : 0, held ? 2 : 0);
        ks_voice_render(v, p, trigger && i == 0, out + i, KS_BLOCK_SIZE);
    }
}

static struct ks_params params_for(double hz)
{
    struct ks_params p;
    memset(&p, 0, sizeof p);
    p.period_q8 = (uint32_t)(RATE * 256.0 / hz + 0.5);
    p.exciter = KS_EXC_NOISE;
    p.bright = 127;
    p.decay = 120;
    p.tone = 1;
    p.velocity = 127;
    return p;
}

/** Hann-windowed DFT magnitude at one frequency over `len` samples. */
static double dft_mag(const int32_t *x, unsigned len, double hz)
{
    double re = 0, im = 0, w = 2 * M_PI * hz / RATE;
    unsigned i;
    for (i = 0; i < len; ++i) {
        double hann = 0.5 - 0.5 * cos(2 * M_PI * i / (len - 1));
        double s = x[i] / 65536.0 * hann;
        re += s * cos(w * i);
        im -= s * sin(w * i);
    }
    return sqrt(re * re + im * im);
}

/** Measured fundamental: the strongest DFT bin within +/-50 cents of the
 * expected pitch, scanned in 0.25-cent steps. Unlike an autocorrelation,
 * upper partials cannot pull the result, as long as the window is long
 * enough to separate the fundamental from them: low notes need more. */
static double measure_hz_over(const int32_t *x, double expect_hz,
                              unsigned len)
{
    double best_hz = expect_hz, best = -1, c;
    for (c = -50.0; c <= 50.0; c += 0.25) {
        double hz = expect_hz * pow(2.0, c / 1200.0);
        double m = dft_mag(x, len, hz);
        if (m > best) { best = m; best_hz = hz; }
    }
    return best_hz;
}

static double measure_hz(const int32_t *x, double expect_hz)
{ return measure_hz_over(x, expect_hz, 8192); }

static double cents(double measured, double expected)
{ return 1200.0 * log2(measured / expected); }

static uint64_t energy(const int32_t *x, unsigned n)
{
    uint64_t total = 0;
    unsigned i;
    for (i = 0; i < n; ++i) total += (uint64_t)llabs((long long)(x[i] / 65536));
    return total;
}

static void test_tuning(void)
{
    static const double notes[] = { 55.0, 110.0, 440.0, 1760.0 };
    static const uint8_t tones[] = { 0, 1, 2, 3, 4 };
    unsigned i, j;
    for (i = 0; i < 4; ++i) {
        for (j = 0; j < 5; ++j) {
            struct ks_voice v;
            struct ks_params p = params_for(notes[i]);
            double hz, error;
            p.tone = tones[j];
            ks_voice_init(&v);
            render(&v, &p, 1, 1, buf_a, 16384);
            hz = measure_hz(buf_a + 8192, notes[i]);
            error = cents(hz, notes[i]);
            printf("tune %7.1f Hz tone %u: %8.2f Hz (%+.2f cents)\n",
                   notes[i], tones[j], hz, error);
            assert(fabs(error) < 5.0);
        }
    }
}

/* STIFF tunes by its DC phase delay, so the fundamental of a very high
 * note goes slightly sharp (about +8 cents at 1760 Hz, STIFF 127). Up to
 * 880 Hz it stays within the same bound as the ideal string. */
static void test_stiff_tuning(void)
{
    static const double notes[] = { 55.0, 220.0, 880.0 };
    unsigned i;
    for (i = 0; i < 3; ++i) {
        struct ks_voice v;
        struct ks_params p = params_for(notes[i]);
        double error;
        p.stiff = 127;
        ks_voice_init(&v);
        render(&v, &p, 1, 1, buf_a, 16384);
        error = cents(measure_hz(buf_a + 8192, notes[i]), notes[i]);
        printf("stiff 127 %6.1f Hz: %+.2f cents\n", notes[i], error);
        assert(fabs(error) < 5.0);
    }
}

/* Notes too long for the line run the loop slower instead of folding up
 * an octave; the window is long enough to tell them from their octave. */
static void test_low_notes(void)
{
    static const double notes[] = { 24.5, 20.0, 15.0, 10.0 };
    unsigned i;
    for (i = 0; i < 4; ++i) {
        struct ks_voice v;
        struct ks_params p = params_for(notes[i]);
        double hz;
        p.decay = 127;
        ks_voice_init(&v);
        render(&v, &p, 1, 1, buf_a, N);
        hz = measure_hz_over(buf_a + 8192, notes[i], 32768);
        printf("%5.1f Hz plays %.2f Hz\n", notes[i], hz);
        assert(fabs(cents(hz, notes[i])) < 5.0);
    }
}

/** Level in dB of a stretch of output (RMS). */
static double level_db(const int32_t *x, unsigned n)
{
    double sum = 0;
    unsigned i;
    for (i = 0; i < n; ++i) {
        double s = x[i] / 65536.0;
        sum += s * s;
    }
    return 10.0 * log10(sum / n + 1e-12);
}

/* DECAY is a T60 in seconds: the same knob gives the same decay time on a
 * low and a high note. Measured on a lossless loop (TONE 4, STIFF 0), as
 * the slope between two windows, against Rings' curve. */
static void test_decay_is_t60(void)
{
    static const double notes[] = { 110.0, 880.0 };
    static const uint8_t decays[] = { 40, 70 };
    unsigned i, j;
    for (j = 0; j < 2; ++j) {
        double d = decays[j] / 127.0;
        double expect = 0.07 * pow(2.0, 8.0 * d * (2.0 - d));
        for (i = 0; i < 2; ++i) {
            struct ks_voice v;
            struct ks_params p = params_for(notes[i]);
            double slope, t60;
            p.decay = decays[j];
            p.tone = 4; /* no loop lowpass */
            ks_voice_init(&v);
            render(&v, &p, 1, 1, buf_a, N);
            slope = (level_db(buf_a + 4800, 4800)
                     - level_db(buf_a + 19200, 4800)) / 0.3;
            t60 = 60.0 / slope;
            printf("decay %3u at %5.0f Hz: T60 %.3f s (expected %.3f s)\n",
                   decays[j], notes[i], t60, expect);
            assert(fabs(t60 / expect - 1.0) < 0.15);
        }
    }
}

/** Spectral centroid in multiples of the fundamental: DFT magnitudes of
 * the first 24 harmonics, weighted by harmonic number. */
static double centroid(const int32_t *x, double hz)
{
    double num = 0, den = 0;
    unsigned h;
    for (h = 1; h <= 24 && h * hz < RATE / 2; ++h) {
        double m = dft_mag(x, 8192, h * hz);
        num += h * m;
        den += m;
    }
    return num / den;
}

/* TONE's cutoff tracks the note: each step is at least as bright as the
 * one below (two steps match once both filters open past half the loop
 * rate), and a step gives about the same harmonic balance on a low and a
 * high note. Every trip darkens the harmonics a little, so the two notes
 * are compared after the same number of periods, not the same time. */
static void test_tone_tracks_the_note(void)
{
    static const double notes[] = { 110.0, 440.0 };
    double c[2][5];
    unsigned i, tone;
    for (i = 0; i < 2; ++i) {
        unsigned start = (unsigned)(30.0 * RATE / notes[i]);
        for (tone = 0; tone < 5; ++tone) {
            struct ks_voice v;
            struct ks_params p = params_for(notes[i]);
            p.tone = (uint8_t)tone;
            p.decay = 90;
            ks_voice_init(&v);
            render(&v, &p, 1, 1, buf_a, start + 8192);
            c[i][tone] = centroid(buf_a + start, notes[i]);
            printf("tone %u at %3.0f Hz: centroid %.2f harmonics\n",
                   tone, notes[i], c[i][tone]);
            if (tone) assert(c[i][tone] >= c[i][tone - 1]);
        }
        assert(c[i][4] > 2.0 * c[i][0]);
    }
    for (tone = 0; tone < 3; ++tone)
        assert(fabs(c[1][tone] / c[0][tone] - 1.0) < 0.3);
}

/* As in Rings, TONE's lowpass shortens high notes somewhat, but DECAY
 * still rules: a high note at TONE 0 keeps a good part of its T60. */
static void test_tone_keeps_decay(void)
{
    struct ks_voice v;
    struct ks_params p = params_for(880.0);
    double d = 70 / 127.0, expect = 0.07 * pow(2.0, 8.0 * d * (2.0 - d));
    double t60;
    p.decay = 70;
    p.tone = 0;
    ks_voice_init(&v);
    render(&v, &p, 1, 1, buf_a, N);
    t60 = 60.0 / ((level_db(buf_a + 4800, 4800)
                   - level_db(buf_a + 19200, 4800)) / 0.3);
    printf("tone 0 at 880 Hz, decay 70: T60 %.3f s (DECAY alone %.3f s)\n",
           t60, expect);
    assert(t60 > 0.4 * expect && t60 < 1.05 * expect);
}

static void test_deterministic_and_bounded(void)
{
    struct ks_voice va, vb;
    struct ks_params p = params_for(220.0);
    unsigned i;
    p.decay = 127; p.tone = 4; p.stiff = 127; p.pos = 0;
    ks_voice_init(&va);
    ks_voice_init(&vb);
    render(&va, &p, 1, 1, buf_a, N);
    render(&vb, &p, 1, 1, buf_b, N);
    assert(!memcmp(buf_a, buf_b, sizeof buf_a));
    for (i = 0; i < N; ++i) {
        assert(buf_a[i] <= 8191 * 65536);
        assert(buf_a[i] >= -8192 * 65536);
    }
}

static void test_decay_control(void)
{
    struct ks_voice v;
    struct ks_params p = params_for(220.0);
    uint64_t short_tail, long_tail;
    p.decay = 0;
    ks_voice_init(&v);
    render(&v, &p, 1, 1, buf_a, N);
    short_tail = energy(buf_a + 24000, 12000);
    p.decay = 127;
    ks_voice_init(&v);
    render(&v, &p, 1, 1, buf_a, N);
    long_tail = energy(buf_a + 24000, 12000);
    printf("tail energy: decay 0 %llu, decay 127 %llu\n",
           (unsigned long long)short_tail, (unsigned long long)long_tail);
    assert(long_tail > 100000);
    assert(short_tail * 100 < long_tail);
}

static void test_every_exciter_sounds(void)
{
    unsigned exciter;
    for (exciter = 0; exciter < 4; ++exciter) {
        struct ks_voice v;
        struct ks_params p = params_for(220.0);
        uint64_t e;
        p.exciter = (uint8_t)exciter;
        p.pos = 40;
        p.bright = 80;
        ks_voice_init(&v);
        render(&v, &p, 1, 1, buf_a, 12000);
        e = energy(buf_a, 12000);
        printf("exciter %u energy %llu\n", exciter, (unsigned long long)e);
        assert(e > 200000);
    }
}

static void test_controls_change_sound(void)
{
    static const unsigned offsets[] = { 1, 2, 3 }; /* pos, stiff, bright */
    unsigned k;
    for (k = 0; k < 3; ++k) {
        struct ks_voice va, vb;
        struct ks_params pa = params_for(220.0), pb = params_for(220.0);
        if (offsets[k] == 1) pb.pos = 64;
        if (offsets[k] == 2) pb.stiff = 127;
        if (offsets[k] == 3) pb.bright = 10;
        ks_voice_init(&va);
        ks_voice_init(&vb);
        render(&va, &pa, 1, 1, buf_a, 4800);
        render(&vb, &pb, 1, 1, buf_b, 4800);
        assert(memcmp(buf_a, buf_b, 4800 * sizeof buf_a[0]));
    }
}

static void test_release_sleeps(void)
{
    struct ks_voice v;
    struct ks_params p = params_for(220.0);
    unsigned i;
    p.decay = 127;
    ks_voice_init(&v);
    render(&v, &p, 1, 1, buf_a, 4800);
    /* AMP released: 32 quiet blocks, then a 128-sample fade. */
    render(&v, &p, 0, 0, buf_a, 32 * 40);
    assert(v.sleeping);
    for (i = 32 * 36; i < 32 * 40; ++i) assert(buf_a[i] == 0);
    /* A new trigger wakes it into a fresh string. */
    render(&v, &p, 1, 1, buf_a, 4800);
    assert(!v.sleeping);
    assert(energy(buf_a, 4800) > 100000);
}

static void test_decayed_string_sleeps_while_held(void)
{
    struct ks_voice v;
    struct ks_params p = params_for(880.0);
    p.decay = 0;
    p.tone = 0;
    ks_voice_init(&v);
    render(&v, &p, 1, 1, buf_a, N);
    assert(v.sleeping);
}

static void test_bow_sustains_until_release(void)
{
    struct ks_voice v;
    struct ks_params p = params_for(220.0);
    p.exciter = KS_EXC_BOW;
    p.decay = 60;
    ks_voice_init(&v);
    render(&v, &p, 1, 1, buf_a, N);
    assert(energy(buf_a + 36000, 12000) > 50000);
    render(&v, &p, 0, 0, buf_a, N);
    assert(v.sleeping);
}

static void test_retrigger_adds_to_ringing_string(void)
{
    struct ks_voice va, vb;
    struct ks_params p = params_for(220.0);
    p.decay = 127;
    p.exciter = KS_EXC_PLUCK;
    ks_voice_init(&va);
    ks_voice_init(&vb);
    render(&va, &p, 1, 1, buf_a, 4800);
    render(&vb, &p, 1, 1, buf_b, 4800);
    /* Retrigger one voice: it must differ from a fresh pluck, because the
     * old vibration is kept and the excitation adds to it. */
    render(&va, &p, 1, 1, buf_a, 4800);
    ks_voice_init(&vb);
    render(&vb, &p, 1, 1, buf_b, 4800);
    assert(memcmp(buf_a, buf_b, 4800 * sizeof buf_a[0]));
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    test_tuning();
    test_stiff_tuning();
    test_low_notes();
    test_decay_is_t60();
    test_tone_tracks_the_note();
    test_tone_keeps_decay();
    test_deterministic_and_bounded();
    test_decay_control();
    test_every_exciter_sounds();
    test_controls_change_sound();
    test_release_sleeps();
    test_decayed_string_sleeps_while_held();
    test_bow_sustains_until_release();
    test_retrigger_adds_to_ringing_string();
    puts("karplus: all tests passed");
    return 0;
}
