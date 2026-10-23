/**
  * Гироскоп и акселерометр BMI270 на SPI2, CS = PC13.
  * Оси — оси чипа. На плате чип повёрнут (INAV: CW270_DEG_FLIP).
  */
#ifndef DRV_BMI270_H
#define DRV_BMI270_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
  int32_t gyro_mdps[3];     /* угловая скорость X, Y, Z, милли-°/с */
  int32_t accel_mg[3];      /* ускорение X, Y, Z, милли-g */
  int32_t temperature_cdeg; /* температура чипа, сотые °C */
} imu_sample_t;

bool bmi270_read_id(uint8_t *chip_id);
bool bmi270_init(void);
bool bmi270_read(imu_sample_t *sample);

#endif /* DRV_BMI270_H */
