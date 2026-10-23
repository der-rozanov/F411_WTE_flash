/**
  * Драйвер OSD AT7456E (совместим с MAX7456), SPI2, CS = PB12.
  * Даташит: MAX7456 (Maxim), таблица регистров; AT7456E повторяет её.
  *
  * Регистра ID у чипа нет. Присутствие проверяется так же, как в Betaflight:
  * регистр OSDM после сброса равен 0x1B, а мы его не меняем.
  */
#include "spi.h"
#include "drv_at7456.h"

#define REG_OSDM_READ         0x8Cu  /* адрес OSDM 0x0C + бит чтения 0x80 */
#define OSDM_RESET_VALUE      0x1Bu
#define SPI_TIMEOUT_MS        10u

bool at7456_read_osdm(uint8_t *value)
{
  uint8_t address = REG_OSDM_READ;

  HAL_GPIO_WritePin(OSD_CS_GPIO_Port, OSD_CS_Pin, GPIO_PIN_RESET);  /* выбрать OSD */
  bool ok = (HAL_SPI_Transmit(&hspi2, &address, 1, SPI_TIMEOUT_MS) == HAL_OK)
         && (HAL_SPI_Receive(&hspi2, value, 1, SPI_TIMEOUT_MS) == HAL_OK);
  HAL_GPIO_WritePin(OSD_CS_GPIO_Port, OSD_CS_Pin, GPIO_PIN_SET);    /* отпустить шину */
  return ok;
}

bool at7456_is_present(void)
{
  uint8_t osdm = 0;
  return at7456_read_osdm(&osdm) && (osdm == OSDM_RESET_VALUE);
}
