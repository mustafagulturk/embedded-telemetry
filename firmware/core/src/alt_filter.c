#include "alt_filter.h"

#include <stddef.h>

alt_filter_cfg_t alt_filter_default_cfg(void)
{
    alt_filter_cfg_t cfg;

    cfg.alpha = 0.08f;
    cfg.beta = 0.0035f;
    cfg.max_residual_m = 10.0f;
    return cfg;
}

bool alt_filter_init(alt_filter_t *f, const alt_filter_cfg_t *cfg)
{
    if ((f == NULL) || (cfg == NULL) || (cfg->alpha <= 0.0f) || (cfg->alpha > 1.0f) ||
        (cfg->beta < 0.0f) || (cfg->beta > 1.0f)) {
        return false;
    }
    f->cfg = *cfg;
    alt_filter_reset(f);
    return true;
}

void alt_filter_reset(alt_filter_t *f)
{
    if (f != NULL) {
        f->alt_m = 0.0f;
        f->vel_mps = 0.0f;
        f->initialised = false;
    }
}

float alt_filter_update(alt_filter_t *f, float meas_m, float dt_s)
{
    float pred;
    float r;

    if (f == NULL) {
        return meas_m;
    }
    if (!f->initialised) {
        f->alt_m = meas_m;
        f->vel_mps = 0.0f;
        f->initialised = true;
        return f->alt_m;
    }
    if (dt_s <= 0.0f) {
        return f->alt_m;
    }

    pred = f->alt_m + (f->vel_mps * dt_s);
    r = meas_m - pred;
    if (f->cfg.max_residual_m > 0.0f) {
        if (r > f->cfg.max_residual_m) {
            r = f->cfg.max_residual_m;
        } else if (r < -f->cfg.max_residual_m) {
            r = -f->cfg.max_residual_m;
        } else {
            /* within bounds */
        }
    }
    f->alt_m = pred + (f->cfg.alpha * r);
    f->vel_mps += (f->cfg.beta * r) / dt_s;
    return f->alt_m;
}
