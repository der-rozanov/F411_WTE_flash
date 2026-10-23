/**
  * Драйвер барометра SPL06-001 (Goertek), I2C1.
  * Даташит: SPL06-001 Datasheet v1.0, разделы 4.9 (пересчёт) и 7 (регистры).
  *
  * Режим: непрерывное измерение, давление 32 раза/с с передискретизацией ×16,
  * температура 32 раза/с без передискретизации, внешний (MEMS) датчик температуры.
  * Пересчёт сырых значений в паскали — по формулам даташита, в float (FPU есть).
  */
#include "i2c.h"
#include "drv_spl06.h"

#define SPL06_ADDRESS_LOW     0x76u  /* SDO на земле */
#define SPL06_ADDRESS_HIGH    0x77u  /* SDO на питании */

#define REG_PSR_B2            0x00u  /* дальше подряд: давление 3 байта, температура 3 байта */
#define REG_PRS_CFG           0x06u
#define REG_TMP_CFG           0x07u
#define REG_MEAS_CFG          0x08u
#define REG_CFG_REG           0x09u
#define REG_ID                0x0Du
#define REG_COEF              0x10u  /* 18 байт калибровки c0..c30 */

#define ID_VALUE              0x10u
#define MEAS_CFG_COEF_READY   0x80u
#define MEAS_CFG_SENSOR_READY 0x40u
#define MEAS_CFG_CONTINUOUS_PRESSURE_TEMPERATURE 0x07u

#define PRS_CFG_32HZ_X16      0x54u  /* PM_RATE = 101 (32/с), PM_PRC = 0100 (×16) */
#define TMP_CFG_EXT_32HZ_X1   0xD0u  /* TMP_EXT = 1, TMP_RATE = 101 (32/с), TMP_PRC = 000 (×1) */
#define CFG_REG_PRESSURE_SHIFT 0x04u /* обязателен при передискретизации больше ×8 */

/* Масштабные коэффициенты kP, kT для выбранной передискретизации (таблица 4) */
#define PRESSURE_SCALE_X16    253952.0f
#define TEMPERATURE_SCALE_X1  524288.0f

#define COEF_SIZE             18u
#define RESULT_SIZE           6u
#define I2C_TIMEOUT_MS        10u
#define I2C_PROBE_TRIES       2u
#define READY_WAIT_MS         100u   /* чип готов через ~40 мс после включения */

typedef struct
{
  int32_t c0, c1, c00, c10, c01, c11, c20, c21, c30;
} spl06_coefficients_t;

static uint8_t device_address;  /* 7 бит; 0 = чип не найден */
static spl06_coefficients_t coef;

static bool read_registers(uint8_t first_reg, uint8_t *values, uint16_t count)
{
  return HAL_I2C_Mem_Read(&hi2c1, (uint16_t)(device_address << 1), first_reg,
                          I2C_MEMADD_SIZE_8BIT, values, count, I2C_TIMEOUT_MS) == HAL_OK;
}

static bool write_register(uint8_t reg, uint8_t value)
{
  return HAL_I2C_Mem_Write(&hi2c1, (uint16_t)(device_address << 1), reg,
                           I2C_MEMADD_SIZE_8BIT, &value, 1, I2C_TIMEOUT_MS) == HAL_OK;
}

/* Знаковое число из младших bits бит: калибровка лежит в 12-, 16-, 20- и 24-битных полях */
static int32_t sign_extend(uint32_t value, uint32_t bits)
{
  uint32_t sign_bit = 1u << (bits - 1u);
  return (int32_t)(value ^ sign_bit) - (int32_t)sign_bit;
}

static void parse_coefficients(const uint8_t *b)
{
  coef.c0  = sign_extend(((uint32_t)b[0] << 4) | (b[1] >> 4), 12);
  coef.c1  = sign_extend(((uint32_t)(b[1] & 0x0Fu) << 8) | b[2], 12);
  coef.c00 = sign_extend(((uint32_t)b[3] << 12) | ((uint32_t)b[4] << 4) | (b[5] >> 4), 20);
  coef.c10 = sign_extend(((uint32_t)(b[5] & 0x0Fu) << 16) | ((uint32_t)b[6] << 8) | b[7], 20);
  coef.c01 = sign_extend(((uint32_t)b[8] << 8) | b[9], 16);
  coef.c11 = sign_extend(((uint32_t)b[10] << 8) | b[11], 16);
  coef.c20 = sign_extend(((uint32_t)b[12] << 8) | b[13], 16);
  coef.c21 = sign_extend(((uint32_t)b[14] << 8) | b[15], 16);
  coef.c30 = sign_extend(((uint32_t)b[16] << 8) | b[17], 16);
}

/* Найти чип по одному из двух адресов и сверить ID */
static bool find_device(void)
{
  const uint8_t candidates[] = { SPL06_ADDRESS_LOW, SPL06_ADDRESS_HIGH };

  for (uint32_t i = 0; i < sizeof(candidates); i++)
  {
    device_address = candidates[i];
    uint8_t id = 0;
    if ((HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(device_address << 1), I2C_PROBE_TRIES, I2C_TIMEOUT_MS) == HAL_OK)
        && read_registers(REG_ID, &id, 1) && (id == ID_VALUE))
    {
      return true;
    }
  }
  device_address = 0;
  return false;
}

static bool wait_until_ready(void)
{
  uint32_t start_ms = HAL_GetTick();
  const uint8_t ready = MEAS_CFG_COEF_READY | MEAS_CFG_SENSOR_READY;
  uint8_t status = 0;

  while ((HAL_GetTick() - start_ms) < READY_WAIT_MS)
  {
    if (read_registers(REG_MEAS_CFG, &status, 1) && ((status & ready) == ready))
    {
      return true;
    }
  }
  return false;
}

/* Блокирует до 100 мс: звать один раз при старте */
bool spl06_init(void)
{
  uint8_t raw_coef[COEF_SIZE];

  if (!find_device() || !wait_until_ready() || !read_registers(REG_COEF, raw_coef, sizeof(raw_coef)))
  {
    return false;
  }
  parse_coefficients(raw_coef);

  return write_register(REG_PRS_CFG, PRS_CFG_32HZ_X16)
      && write_register(REG_TMP_CFG, TMP_CFG_EXT_32HZ_X1)
      && write_register(REG_CFG_REG, CFG_REG_PRESSURE_SHIFT)
      && write_register(REG_MEAS_CFG, MEAS_CFG_CONTINUOUS_PRESSURE_TEMPERATURE);
}

uint8_t spl06_address(void)
{
  return device_address;
}

bool spl06_read(int32_t *pressure_pa, int32_t *temperature_cdeg)
{
  uint8_t raw[RESULT_SIZE];

  if ((device_address == 0u) || !read_registers(REG_PSR_B2, raw, sizeof(raw)))
  {
    return false;
  }

  int32_t pressure_raw    = sign_extend(((uint32_t)raw[0] << 16) | ((uint32_t)raw[1] << 8) | raw[2], 24);
  int32_t temperature_raw = sign_extend(((uint32_t)raw[3] << 16) | ((uint32_t)raw[4] << 8) | raw[5], 24);

  /* Формулы из раздела 4.9.1 и 4.9.2 даташита */
  float p = (float)pressure_raw / PRESSURE_SCALE_X16;
  float t = (float)temperature_raw / TEMPERATURE_SCALE_X1;

  float pressure = (float)coef.c00
                 + p * ((float)coef.c10 + p * ((float)coef.c20 + p * (float)coef.c30))
                 + t * (float)coef.c01
                 + t * p * ((float)coef.c11 + p * (float)coef.c21);
  float temperature = (float)coef.c0 * 0.5f + (float)coef.c1 * t;

  *pressure_pa = (int32_t)pressure;
  *temperature_cdeg = (int32_t)(temperature * 100.0f);
  return true;
}
