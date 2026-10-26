/**
 * @file alt_filter.h
 * @brief Alpha-beta (g-h) filter for barometric altitude.
 *
 * Tracks altitude and vertical speed, so a steady climb does not produce the
 * constant lag of a plain moving average. Residuals larger than
 * @c max_residual_m are clamped, which bounds the effect of single outliers.
 */
#ifndef ALT_FILTER_H
#define ALT_FILTER_H

#include <stdbool.h>

typedef struct {
    float alpha;          /* position gain, 0..1 */
    float beta;           /* velocity gain, 0..1 */
    float max_residual_m; /* outlier clamp, <= 0 disables */
} alt_filter_cfg_t;

typedef struct {
    alt_filter_cfg_t cfg;
    float alt_m;
    float vel_mps;
    bool initialised;
} alt_filter_t;

/** Sensible defaults for a 100 Hz, ~0.5 m RMS noisy baro. */
alt_filter_cfg_t alt_filter_default_cfg(void);

bool alt_filter_init(alt_filter_t *f, const alt_filter_cfg_t *cfg);

/** Forget state; the next sample re-seeds the filter. */
void alt_filter_reset(alt_filter_t *f);

/** Feed one measurement taken @p dt_s after the previous one. Returns the estimate. */
float alt_filter_update(alt_filter_t *f, float meas_m, float dt_s);

#endif /* ALT_FILTER_H */
