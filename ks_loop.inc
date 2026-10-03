/* SPDX-License-Identifier: MIT
 *
 * The per-sample render loop. karplus.c includes this file twice, with
 * KS_LOOP_NAME and KS_LOOP_SECTION set: once into normal code and once
 * into .fast (on-chip SRAM under FAST AUDIO), so both copies are the same
 * source and compute the same samples.
 */

/**
 * @brief Render up to n samples of an awake voice.
 * @param peak updated with the loop's largest magnitude.
 * @return n, or the number of samples written when the fade-out ends and
 *         the voice goes to sleep (the caller zeroes the rest).
 */
static __attribute__((noinline, section(KS_LOOP_SECTION))) uint32_t
KS_LOOP_NAME(struct ks_voice *v, const struct ks_loop *loop, uint8_t bright,
             int32_t *out, uint32_t n, int32_t *peak)
{
    uint32_t i;
    for (i = 0; i < n; ++i) {
        int32_t y;
        /* At full rate this steps every sample and outputs src_cur exactly;
         * slower, it steps when the phase wraps and interpolates. */
        v->src_phase += loop->rate;
        if (v->src_phase > KS_RATE_FULL) {
            v->src_phase -= KS_RATE_FULL;
            v->src_prev = v->src_cur;
            v->src_cur = ks_step(v, loop, bright, peak);
        }
        /* |cur - prev| <= 65535 and phase <= 2^15: fits 32 bits. */
        y = v->src_prev
            + (((v->src_cur - v->src_prev) * v->src_phase) >> 15);
        /* A full-scale string reaches 0.25 FS, Sophie's source level. */
        y >>= 2;
        if (v->fade_left) {
            y = (y * v->fade_left) >> 7;
            if (--v->fade_left == 0) {
                v->sleeping = 1;
                out[i] = 0;
                return i + 1;
            }
        }
        out[i] = y * 65536;
    }
    return n;
}
