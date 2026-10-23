/**
  * Драйвер BMI270 (Bosch): гироскоп + акселерометр, SPI2, CS = PC13.
  * Даташит: BST-BMI270-DS000, разделы 4.4 (запуск), 5.2 (регистры).
  *
  * Особенности SPI у BMI270: после включения чип в режиме I2C, первое чтение
  * по SPI холостое и переключает его на SPI. При чтении первый байт после
  * адреса пустой, данные идут со второго.
  * Без загрузки конфигурационного файла Bosch (bmi270_config.c) чип данные не выдаёт.
  *
  * Режим: гироскоп ±2000 °/с, 200 Гц; акселерометр ±16 g, 100 Гц.
  */
#include "spi.h"
#include "drv_bmi270.h"

extern const uint8_t bmi270_config_file[];
extern const uint32_t bmi270_config_file_size;

#define REG_CHIP_ID           0x00u
#define REG_DATA_ACC_X_LSB    0x0Cu  /* дальше подряд: ACC XYZ, GYR XYZ, по 2 байта */
#define REG_INTERNAL_STATUS   0x21u
#define REG_TEMPERATURE_LSB   0x22u
#define REG_ACC_CONF          0x40u
#define REG_ACC_RANGE         0x41u
#define REG_GYR_CONF          0x42u
#define REG_GYR_RANGE         0x43u
#define REG_INIT_CTRL         0x59u
#define REG_INIT_DATA         0x5Eu
#define REG_PWR_CONF          0x7Cu
#define REG_PWR_CTRL          0x7Du

#define CHIP_ID_VALUE         0x24u
#define INTERNAL_STATUS_MASK  0x0Fu
#define INTERNAL_STATUS_INIT_OK 0x01u
#define PWR_CONF_POWER_SAVE_OFF 0x00u
#define PWR_CONF_NORMAL       0x02u  /* без энергосбережения, FIFO self wake-up */
#define INIT_CTRL_START       0x00u
#define INIT_CTRL_DONE        0x01u
#define PWR_CTRL_ACC_GYR_TEMP 0x0Eu
#define ACC_CONF_100HZ        0xA8u  /* значения из примера даташита, раздел 4.4 */
#define GYR_CONF_200HZ        0xA9u
#define ACC_RANGE_16G         0x03u
#define GYR_RANGE_2000DPS     0x00u

/* Чувствительность: ±16 g = 2048 LSB/g; ±2000 °/с = 16,384 LSB/(°/с) = 32768 LSB на 2000 °/с */
#define ACCEL_LSB_PER_G       2048
#define GYRO_MDPS_NUMERATOR   15625  /* 2 000 000 мград/с / 32768 = 15625 / 256 */
#define GYRO_MDPS_DENOMINATOR 256
#define TEMP_LSB_PER_DEG      512    /* 1/512 °C на LSB */
#define TEMP_OFFSET_CDEG      2300   /* 0 LSB = 23 °C */
#define TEMP_INVALID          ((int16_t)0x8000)

#define SPI_READ_FLAG         0x80u
#define SPI_DUMMY_BYTES       1u     /* пустой байт после адреса при чтении */
#define SPI_TIMEOUT_MS        10u
#define CONFIG_UPLOAD_TIMEOUT_MS 100u  /* 8 КБ на 6 Мбит/с ~ 11 мс */
#define POWER_SAVE_EXIT_MS    1u     /* даташит: 450 мкс */
#define INIT_WAIT_MS          25u    /* даташит: проверить статус через 20 мс */
#define SENSOR_START_MS       50u    /* гироскопу нужно ~45 мс на запуск */

#define DATA_BLOCK_SIZE       12u
#define DATA_ACCEL_OFFSET     0u
#define DATA_GYRO_OFFSET      6u
#define BYTES_PER_AXIS        2u
#define READ_BUFFER_SIZE      (1u + SPI_DUMMY_BYTES + DATA_BLOCK_SIZE)

static bool read_registers(uint8_t first_reg, uint8_t *values, uint16_t count)
{
  uint8_t tx[READ_BUFFER_SIZE] = { first_reg | SPI_READ_FLAG };
  uint8_t rx[READ_BUFFER_SIZE] = { 0 };
  uint16_t length = (uint16_t)(1u + SPI_DUMMY_BYTES + count);

  if (length > READ_BUFFER_SIZE)
  {
    return false;
  }
  HAL_GPIO_WritePin(IMU_CS_GPIO_Port, IMU_CS_Pin, GPIO_PIN_RESET);  /* выбрать гироскоп */
  bool ok = (HAL_SPI_TransmitReceive(&hspi2, tx, rx, length, SPI_TIMEOUT_MS) == HAL_OK);
  HAL_GPIO_WritePin(IMU_CS_GPIO_Port, IMU_CS_Pin, GPIO_PIN_SET);    /* отпустить шину */

  for (uint16_t i = 0; i < count; i++)
  {
    values[i] = rx[1u + SPI_DUMMY_BYTES + i];
  }
  return ok;
}

/* Запись подряд нескольких байт начиная с регистра reg */
static bool write_registers(uint8_t reg, const uint8_t *values, uint16_t count, uint32_t timeout_ms)
{
  HAL_GPIO_WritePin(IMU_CS_GPIO_Port, IMU_CS_Pin, GPIO_PIN_RESET);
  bool ok = (HAL_SPI_Transmit(&hspi2, &reg, 1, SPI_TIMEOUT_MS) == HAL_OK)
         && (HAL_SPI_Transmit(&hspi2, (uint8_t *)values, count, timeout_ms) == HAL_OK);
  HAL_GPIO_WritePin(IMU_CS_GPIO_Port, IMU_CS_Pin, GPIO_PIN_SET);
  return ok;
}

static bool write_register(uint8_t reg, uint8_t value)
{
  return write_registers(reg, &value, 1, SPI_TIMEOUT_MS);
}

/* Два байта младшим вперёд -> знаковое число */
static int16_t little_endian_int16(const uint8_t *bytes)
{
  return (int16_t)(((uint16_t)bytes[1] << 8) | bytes[0]);
}

bool bmi270_read_id(uint8_t *chip_id)
{
  (void)read_registers(REG_CHIP_ID, chip_id, 1);  /* холостое: переводит чип в SPI */
  return read_registers(REG_CHIP_ID, chip_id, 1);
}

/* Запуск по разделу 4.4 даташита. Блокирует примерно на 80 мс: звать один раз при старте */
bool bmi270_init(void)
{
  uint8_t chip_id = 0;
  uint8_t status = 0;

  if (!bmi270_read_id(&chip_id) || (chip_id != CHIP_ID_VALUE))
  {
    return false;
  }

  if (!write_register(REG_PWR_CONF, PWR_CONF_POWER_SAVE_OFF))
  {
    return false;
  }
  HAL_Delay(POWER_SAVE_EXIT_MS);

  bool uploaded = write_register(REG_INIT_CTRL, INIT_CTRL_START)
               && write_registers(REG_INIT_DATA, bmi270_config_file, (uint16_t)bmi270_config_file_size,
                                  CONFIG_UPLOAD_TIMEOUT_MS)
               && write_register(REG_INIT_CTRL, INIT_CTRL_DONE);
  HAL_Delay(INIT_WAIT_MS);

  if (!uploaded || !read_registers(REG_INTERNAL_STATUS, &status, 1)
      || ((status & INTERNAL_STATUS_MASK) != INTERNAL_STATUS_INIT_OK))
  {
    return false;
  }

  bool ok = write_register(REG_PWR_CTRL, PWR_CTRL_ACC_GYR_TEMP)
         && write_register(REG_ACC_CONF, ACC_CONF_100HZ)
         && write_register(REG_ACC_RANGE, ACC_RANGE_16G)
         && write_register(REG_GYR_CONF, GYR_CONF_200HZ)
         && write_register(REG_GYR_RANGE, GYR_RANGE_2000DPS)
         && write_register(REG_PWR_CONF, PWR_CONF_NORMAL);
  HAL_Delay(SENSOR_START_MS);
  return ok;
}

bool bmi270_read(imu_sample_t *sample)
{
  uint8_t data[DATA_BLOCK_SIZE];
  uint8_t temperature[BYTES_PER_AXIS];

  if (!read_registers(REG_DATA_ACC_X_LSB, data, sizeof(data))
      || !read_registers(REG_TEMPERATURE_LSB, temperature, sizeof(temperature)))
  {
    return false;
  }

  for (uint32_t axis = 0; axis < 3u; axis++)
  {
    int32_t accel_raw = little_endian_int16(&data[DATA_ACCEL_OFFSET + BYTES_PER_AXIS * axis]);
    int32_t gyro_raw  = little_endian_int16(&data[DATA_GYRO_OFFSET + BYTES_PER_AXIS * axis]);
    sample->accel_mg[axis]  = accel_raw * 1000 / ACCEL_LSB_PER_G;
    sample->gyro_mdps[axis] = gyro_raw * GYRO_MDPS_NUMERATOR / GYRO_MDPS_DENOMINATOR;
  }

  int16_t temperature_raw = little_endian_int16(temperature);
  if (temperature_raw == TEMP_INVALID)
  {
    return false;
  }
  sample->temperature_cdeg = TEMP_OFFSET_CDEG + (int32_t)temperature_raw * 100 / TEMP_LSB_PER_DEG;
  return true;
}
