/**
  * Выходы S1–S6 платы: TIM4 CH1/CH2 (PB6/PB7) и TIM3 CH3/CH4/CH1/CH2 (PB0/PB1/PB4/PB5).
  * Таймеры настроены в CubeMX на 1 МГц и период 20000 мкс, поэтому значение
  * регистра сравнения = длительность импульса в микросекундах.
  *
  * Проверка без осциллографа: outputs_measure_pulse_us() читает уровень на самом
  * пине (входной регистр GPIO работает и в режиме таймера) и засекает фронты по
  * счётчику того же таймера.
  */
#include "tim.h"
#include "board.h"
#include "drv_outputs.h"

typedef struct
{
  TIM_HandleTypeDef *timer;
  uint32_t channel;
  GPIO_TypeDef *port;
  uint16_t pin;
} output_t;

/* Индекс 0 = S1 ... индекс 5 = S6 (распиновка из таргета INAV) */
static const output_t outputs[OUTPUT_COUNT] =
{
  { &htim4, TIM_CHANNEL_1, GPIOB, GPIO_PIN_6 },  /* S1 мотор */
  { &htim4, TIM_CHANNEL_2, GPIOB, GPIO_PIN_7 },  /* S2 мотор */
  { &htim3, TIM_CHANNEL_3, GPIOB, GPIO_PIN_0 },  /* S3 серва */
  { &htim3, TIM_CHANNEL_4, GPIOB, GPIO_PIN_1 },  /* S4 серва */
  { &htim3, TIM_CHANNEL_1, GPIOB, GPIO_PIN_4 },  /* S5 серва */
  { &htim3, TIM_CHANNEL_2, GPIOB, GPIO_PIN_5 },  /* S6 серва */
};

#define MEASURE_TIMEOUT_MS  (2u * OUTPUT_PERIOD_US / 1000u)  /* два периода ШИМ */

static const output_t *find_output(uint32_t output)
{
  if ((output < 1u) || (output > OUTPUT_COUNT))
  {
    return NULL;
  }
  return &outputs[output - 1u];
}

/* Все выходы запускаются с импульсом 0: на линии Low, как до старта */
bool outputs_init(void)
{
  for (uint32_t i = 0; i < OUTPUT_COUNT; i++)
  {
    __HAL_TIM_SET_COMPARE(outputs[i].timer, outputs[i].channel, OUTPUT_OFF_US);
    if (HAL_TIM_PWM_Start(outputs[i].timer, outputs[i].channel) != HAL_OK)
    {
      return false;
    }
  }
  return true;
}

bool outputs_set_pulse_us(uint32_t output, uint32_t pulse_us)
{
  const output_t *out = find_output(output);
  bool pulse_valid = (pulse_us == OUTPUT_OFF_US) || ((pulse_us >= OUTPUT_MIN_US) && (pulse_us <= OUTPUT_MAX_US));

  if ((out == NULL) || !pulse_valid)
  {
    return false;
  }
  __HAL_TIM_SET_COMPARE(out->timer, out->channel, pulse_us);
  return true;
}

uint32_t outputs_get_pulse_us(uint32_t output)
{
  const output_t *out = find_output(output);
  return (out == NULL) ? 0u : __HAL_TIM_GET_COMPARE(out->timer, out->channel);
}

/* Ждать, пока пин станет нужного уровня. false = не дождались */
static bool wait_for_level(const output_t *out, GPIO_PinState level, uint32_t start_ms)
{
  while (HAL_GPIO_ReadPin(out->port, out->pin) != level)
  {
    if ((HAL_GetTick() - start_ms) > MEASURE_TIMEOUT_MS)
    {
      return false;
    }
  }
  return true;
}

/* Измерить импульс на пине. Блокирует до 40 мс. Линия всё время в Low = импульс 0 */
bool outputs_measure_pulse_us(uint32_t output, uint32_t *pulse_us)
{
  const output_t *out = find_output(output);
  uint32_t start_ms = HAL_GetTick();

  if ((out == NULL) || !wait_for_level(out, GPIO_PIN_RESET, start_ms))
  {
    return false;  /* линия всё время в High — так не должно быть */
  }
  if (!wait_for_level(out, GPIO_PIN_SET, start_ms))
  {
    *pulse_us = 0;
    return true;
  }
  uint32_t rise_us = __HAL_TIM_GET_COUNTER(out->timer);
  if (!wait_for_level(out, GPIO_PIN_RESET, start_ms))
  {
    return false;
  }
  uint32_t fall_us = __HAL_TIM_GET_COUNTER(out->timer);

  /* Счётчик мог перейти через конец периода между фронтами */
  *pulse_us = (fall_us + OUTPUT_PERIOD_US - rise_us) % OUTPUT_PERIOD_US;
  return true;
}
