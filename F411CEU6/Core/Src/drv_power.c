/**
  * Аналоговые входы: ADC1 IN4–IN7 (PA4–PA7), DMA2 Stream0 по кругу.
  * DMA сам обновляет adc_raw[], чтение — просто пересчёт в милливольты.
  * Масштабы — в board.h (из таргета INAV MATEKF411TE).
  */
#include "adc.h"
#include "board.h"
#include "drv_power.h"

/* Порядок совпадает с рангами ADC1 в CubeMX */
enum { ADC_RANK_BATTERY, ADC_RANK_CURRENT, ADC_RANK_RSSI, ADC_RANK_AIRSPEED, ADC_RANK_COUNT };

#define MA_PER_AMP  1000u

static volatile uint16_t adc_raw[ADC_RANK_COUNT];

static uint32_t raw_to_mv(uint16_t raw)
{
  return (uint32_t)raw * ADC_REFERENCE_MV / ADC_FULL_SCALE;
}

bool power_init(void)
{
  return HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_raw, ADC_RANK_COUNT) == HAL_OK;
}

void power_read(power_reading_t *reading)
{
  reading->battery_mv  = raw_to_mv(adc_raw[ADC_RANK_BATTERY]) * VBAT_DIVIDER;
  reading->current_ma  = raw_to_mv(adc_raw[ADC_RANK_CURRENT]) * MA_PER_AMP / CURRENT_MV_PER_AMP;
  reading->rssi_mv     = raw_to_mv(adc_raw[ADC_RANK_RSSI]);
  reading->airspeed_mv = raw_to_mv(adc_raw[ADC_RANK_AIRSPEED]);
}
