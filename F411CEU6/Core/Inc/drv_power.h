/**
  * Аналоговые входы платы через ADC1 + DMA: батарея, ток, RSSI, датчик скорости.
  */
#ifndef DRV_POWER_H
#define DRV_POWER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
  uint32_t battery_mv;
  uint32_t current_ma;
  uint32_t rssi_mv;       /* напряжение на входе RSSI */
  uint32_t airspeed_mv;   /* напряжение на входе датчика скорости */
} power_reading_t;

bool power_init(void);
void power_read(power_reading_t *reading);

#endif /* DRV_POWER_H */
