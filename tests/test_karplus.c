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
    p.damp = 64;
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
 * expected pitch, scanned in 0.25-cent steps over 8192 samples. Unlike an
 * autocorrelation, upper partials cannot pull the result. */
static double measure_hz(const int32_t *x, double expect_hz)
{
    double best_hz = expect_hz, best = -1, c;
    for (c = -50.0; c <= 50.0; c += 0.25) {
        double hz = expect_hz * pow(2.0, c / 1200.0);
        double m = dft_mag(x, 8192, hz);
        if (m > best) { best = m; best_hz = hz; }
    }
    return best_hz;
}

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
    static const uint8_t damps[] = { 0, 64, 127 };
    unsigned i, j;
    for (i = 0; i < 4; ++i) {
        for (j = 0; j < 3; ++j) {
            struct ks_voice v;
            struct ks_params p = params_for(notes[i]);
            double hz, error;
            p.damp = damps[j];
            ks_voice_init(&v);
            render(&v, &p, 1, 1, buf_a, 16384);
            hz = measure_hz(buf_a + 8192, notes[i]);
            error = cents(hz, notes[i]);
            printf("tune %7.1f Hz damp %3u: %8.2f Hz (%+.2f cents)\n",
                   notes[i], damps[j], hz, error);
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

static void test_low_note_folds_up(void)
{
    struct ks_voice v;
    struct ks_params p = params_for(30.0); /* below the line's range */
    double hz;
    ks_voice_init(&v);
    render(&v, &p, 1, 1, buf_a, 16384);
    hz = measure_hz(buf_a + 8192, 60.0);
    printf("30 Hz folds to %.2f Hz\n", hz);
    assert(fabs(cents(hz, 60.0)) < 5.0);
}

static void test_deterministic_and_bounded(void)
{
    struct ks_voice va, vb;
    struct ks_params p = params_for(220.0);
    unsigned i;
    p.decay = 127; p.damp = 0; p.stiff = 127; p.pos = 0;
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
    p.damp = 127;
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
    test_low_note_folds_up();
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
