/**
 * @file altitude.h
 * @brief Barometric altitude (international standard atmosphere, troposphere).
 */
#ifndef ALTITUDE_H
#define ALTITUDE_H

#include <stdint.h>

#define ALTITUDE_P0_STD_PA 101325.0f

/**
 * h = 44330 * (1 - (p / p0)^(1/5.255))  [m]
 * Returns 0 for non-positive inputs.
 */
float altitude_from_pressure(float pressure_pa, float p0_pa);

/** Convert metres to centimetres with rounding and saturation to int32. */
int32_t altitude_m_to_cm(float alt_m);

#endif /* ALTITUDE_H */
