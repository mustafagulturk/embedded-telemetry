#include "altitude.h"

#include <math.h>

float altitude_from_pressure(float pressure_pa, float p0_pa)
{
    if ((pressure_pa <= 0.0f) || (p0_pa <= 0.0f)) {
        return 0.0f;
    }
    return 44330.0f * (1.0f - powf(pressure_pa / p0_pa, 1.0f / 5.255f));
}

int32_t altitude_m_to_cm(float alt_m)
{
    float cm = alt_m * 100.0f;

    if (cm >= 2147483520.0f) {
        return INT32_MAX;
    }
    if (cm <= -2147483520.0f) {
        return INT32_MIN;
    }
    return (int32_t)lroundf(cm);
}
