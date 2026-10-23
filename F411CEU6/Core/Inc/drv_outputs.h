/**
  * Выходы S1–S6: ШИМ 50 Гц, длительность импульса в микросекундах.
  * S1/S2 — моторы (TIM4), S3–S6 — сервы (TIM3). Номера выходов 1..6, как на плате.
  * Импульс 0 = линия в Low, регулятор и серва молчат.
  */
#ifndef DRV_OUTPUTS_H
#define DRV_OUTPUTS_H

#include <stdbool.h>
#include <stdint.h>

#define OUTPUT_OFF_US       0u
#define OUTPUT_MIN_US       500u
#define OUTPUT_MAX_US       2500u

bool outputs_init(void);
bool outputs_set_pulse_us(uint32_t output, uint32_t pulse_us);
uint32_t outputs_get_pulse_us(uint32_t output);
bool outputs_measure_pulse_us(uint32_t output, uint32_t *pulse_us);

#endif /* DRV_OUTPUTS_H */
