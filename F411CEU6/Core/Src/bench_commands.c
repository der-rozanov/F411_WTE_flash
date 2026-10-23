/**
  * Испытательный стенд на Matek F411-WTE: текстовые команды через USB COM-порт.
  * Собирает драйверы вместе и печатает результаты в единицах. Список команд — help.
  *
  * Безопасность моторов (S1/S2): после старта импульс 0, моторы только после arm,
  * импульс не больше MOTOR_TEST_MAX_US, автоматический disarm без команд.
  */
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "i2c.h"
#include "bootloader.h"
#include "board.h"
#include "svc_console.h"
#include "drv_bmi270.h"
#include "drv_spl06.h"
#include "drv_at7456.h"
#include "drv_power.h"
#include "drv_outputs.h"
#include "svc_settings.h"
#include "bench_commands.h"

#define LINE_SIZE              48u
#define WORD_SIZE              16u

#define IMU_INIT_MAX_ATTEMPTS  3u
#define IMU_INIT_RETRY_DELAY_MS 20u

#define MOTOR_COUNT          2u      /* S1, S2 */
#define MOTOR_STOP_US          1000u   /* регулятор видит «газ ноль» */
#define MOTOR_TEST_MAX_US      1200u   /* потолок газа на время испытаний */
#define MOTOR_DISARM_TIMEOUT_MS 10000u /* без команд 10 с -> disarm */

#define I2C_FIRST_ADDRESS      0x08u   /* 0x00–0x07 и 0x78–0x7F зарезервированы */
#define I2C_LAST_ADDRESS       0x77u
#define I2C_SCAN_TRIES         1u
#define I2C_SCAN_TIMEOUT_MS    2u

#define MS_PER_SECOND          1000u
#define DECIMALS_MILLI         3
#define DECIMALS_CENTI         2

static bool imu_ready;
static uint32_t imu_init_attempts;  /* с какой попытки запустился гироскоп при старте */
static bool motors_armed;
static uint32_t last_command_ms;
static uint32_t blink_ms;
static bool settings_loaded;  /* false = во flash пусто, работают значения по умолчанию */

/* Число с фиксированной точкой: print_fixed(-1234, 3) -> "-1.234" */
static void print_fixed(int32_t value, int decimals)
{
  uint32_t divisor = 1;
  for (int i = 0; i < decimals; i++)
  {
    divisor *= 10u;
  }
  uint32_t magnitude = (value < 0) ? (uint32_t)(-value) : (uint32_t)value;
  console_printf("%s%lu.%0*lu", (value < 0) ? "-" : "",
                 (unsigned long)(magnitude / divisor), decimals, (unsigned long)(magnitude % divisor));
}

/* ---------------- датчики ---------------- */

static void command_imu(void)
{
  uint8_t chip_id = 0;
  imu_sample_t sample;

  if (!bmi270_read_id(&chip_id))
  {
    console_printf("imu: SPI error\r\n");
    return;
  }
  console_printf("imu: CHIP_ID = 0x%02X (BMI270 = 0x24)\r\n", chip_id);

  if (!imu_ready)
  {
    imu_ready = bmi270_init();
  }
  if (!imu_ready || !bmi270_read(&sample))
  {
    console_printf("imu: not ready\r\n");
    return;
  }
  console_printf("gyro  dps: ");
  for (int axis = 0; axis < 3; axis++)
  {
    console_printf("%c=", 'X' + axis);
    print_fixed(sample.gyro_mdps[axis], DECIMALS_MILLI);
    console_printf("  ");
  }
  console_printf("\r\naccel g:   ");
  for (int axis = 0; axis < 3; axis++)
  {
    console_printf("%c=", 'X' + axis);
    print_fixed(sample.accel_mg[axis], DECIMALS_MILLI);
    console_printf("  ");
  }
  console_printf("\r\ntemp C: ");
  print_fixed(sample.temperature_cdeg, DECIMALS_CENTI);
  console_printf("\r\n");
}

static void command_baro(void)
{
  int32_t pressure_pa = 0;
  int32_t temperature_cdeg = 0;

  if ((spl06_address() == 0u) && !spl06_init())
  {
    console_printf("baro: SPL06 not found at 0x76/0x77\r\n");
    return;
  }
  if (!spl06_read(&pressure_pa, &temperature_cdeg))
  {
    console_printf("baro: I2C error\r\n");
    return;
  }
  console_printf("baro: SPL06 at 0x%02X, pressure %ld Pa, temp ", spl06_address(), (long)pressure_pa);
  print_fixed(temperature_cdeg, DECIMALS_CENTI);
  console_printf(" C\r\n");
}

static void command_osd(void)
{
  uint8_t osdm = 0;

  if (!at7456_read_osdm(&osdm))
  {
    console_printf("osd: SPI error\r\n");
    return;
  }
  console_printf("osd: OSDM = 0x%02X (AT7456E = 0x1B) -> %s\r\n", osdm,
                 at7456_is_present() ? "found" : "not found");
}

static void command_power(void)
{
  power_reading_t power;

  power_read(&power);
  console_printf("battery ");
  print_fixed((int32_t)power.battery_mv, DECIMALS_MILLI);
  console_printf(" V, current ");
  print_fixed((int32_t)power.current_ma, DECIMALS_MILLI);
  console_printf(" A, rssi %lu mV, airspeed %lu mV\r\n",
                 (unsigned long)power.rssi_mv, (unsigned long)power.airspeed_mv);
}

static void command_i2c_scan(void)
{
  uint32_t found = 0;

  console_printf("i2c1:");
  for (uint8_t address = I2C_FIRST_ADDRESS; address <= I2C_LAST_ADDRESS; address++)
  {
    if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(address << 1), I2C_SCAN_TRIES, I2C_SCAN_TIMEOUT_MS) == HAL_OK)
    {
      console_printf(" 0x%02X", address);
      found++;
    }
  }
  console_printf(found ? "\r\n" : " nothing\r\n");
}

/* ---------------- выходы ---------------- */

static bool is_motor_output(uint32_t output)
{
  return (output >= 1u) && (output <= MOTOR_COUNT);
}

static void set_motors(uint32_t pulse_us)
{
  for (uint32_t motor = 1; motor <= MOTOR_COUNT; motor++)
  {
    outputs_set_pulse_us(motor, pulse_us);
  }
}

static void disarm(const char *reason)
{
  set_motors(OUTPUT_OFF_US);
  motors_armed = false;
  console_printf("disarmed (%s)\r\n", reason);
}

static void command_servo(const char *output_word, const char *pulse_word)
{
  unsigned long output = 0;
  unsigned long pulse_us = 0;

  if ((sscanf(output_word, "%lu", &output) != 1) || (sscanf(pulse_word, "%lu", &pulse_us) != 1))
  {
    console_printf("usage: servo <1-6> <us>, 0 = off\r\n");
    return;
  }
  if (is_motor_output(output))
  {
    bool pulse_allowed = (pulse_us >= MOTOR_STOP_US) && (pulse_us <= MOTOR_TEST_MAX_US);
    if (!motors_armed || !pulse_allowed)
    {
      console_printf("S%lu is a motor: needs arm and pulse %u..%u us\r\n",
                     output, MOTOR_STOP_US, MOTOR_TEST_MAX_US);
      return;
    }
  }
  if (!outputs_set_pulse_us(output, pulse_us))
  {
    console_printf("servo: output 1-6, pulse 0 or %u..%u us\r\n", OUTPUT_MIN_US, OUTPUT_MAX_US);
    return;
  }
  console_printf("S%lu = %lu us\r\n", output, pulse_us);
}

/* Импульс, заданный в таймере, и импульс, измеренный на самом пине */
static void command_outputs(void)
{
  for (uint32_t output = 1; output <= OUTPUT_COUNT; output++)
  {
    uint32_t measured_us = 0;
    console_printf("S%lu: set %lu us, pin ", (unsigned long)output, (unsigned long)outputs_get_pulse_us(output));
    if (outputs_measure_pulse_us(output, &measured_us))
    {
      console_printf("%lu us\r\n", (unsigned long)measured_us);
    }
    else
    {
      console_printf("no signal\r\n");
    }
  }
}

/* PINIO: активный уровень ещё не известен, поэтому команда задаёт уровень пина */
static void command_pinio(const char *number_word, const char *level_word)
{
  GPIO_TypeDef *port = NULL;
  uint16_t pin = 0;

  if (strcmp(number_word, "1") == 0)
  {
    port = VBAT_SW_GPIO_Port;  /* PC15, Vbat на VTX/камеру */
    pin = VBAT_SW_Pin;
  }
  else if (strcmp(number_word, "2") == 0)
  {
    port = CAM_SW_GPIO_Port;   /* PB10, камера 1/2 */
    pin = CAM_SW_Pin;
  }
  bool level_valid = (strcmp(level_word, "0") == 0) || (strcmp(level_word, "1") == 0);

  if ((port == NULL) || !level_valid)
  {
    console_printf("usage: pinio <1|2> <0|1>  (1 = PC15 Vbat, 2 = PB10 camera)\r\n");
    return;
  }
  HAL_GPIO_WritePin(port, pin, (level_word[0] == '1') ? GPIO_PIN_SET : GPIO_PIN_RESET);
  console_printf("pinio %s: pin reads %d\r\n", number_word, (int)HAL_GPIO_ReadPin(port, pin));
}

/* ---------------- общее ---------------- */

static void command_status(void)
{
  console_printf("uptime %lu s\r\n", (unsigned long)(HAL_GetTick() / MS_PER_SECOND));
  console_printf("imu:  %s, boot init attempts %lu\r\n", imu_ready ? "BMI270 running" : "not running",
                 (unsigned long)imu_init_attempts);
  if (spl06_address() != 0u)
  {
    console_printf("baro: SPL06 at 0x%02X\r\n", spl06_address());
  }
  else
  {
    console_printf("baro: not found\r\n");
  }
  console_printf("osd:  %s\r\n", at7456_is_present() ? "AT7456E found" : "not found");
  console_printf("motors: %s\r\n", motors_armed ? "ARMED" : "disarmed");
  console_printf("blink: %lu ms, settings at boot: %s\r\n", (unsigned long)settings.blink_period_ms,
                 settings_loaded ? "from flash" : "defaults");
  command_power();
}

/* ---------------- настройки ---------------- */

/* Без аргумента — показать, с аргументом — поменять в RAM (до save) */
static void command_blink(const char *period_word)
{
  unsigned long period_ms = 0;

  if (period_word[0] == '\0')
  {
    console_printf("blink: %lu ms\r\n", (unsigned long)settings.blink_period_ms);
    return;
  }
  if ((sscanf(period_word, "%lu", &period_ms) != 1) || !settings_blink_period_is_valid(period_ms))
  {
    console_printf("usage: blink <%u..%u ms>\r\n", BLINK_PERIOD_MIN_MS, BLINK_PERIOD_MAX_MS);
    return;
  }
  settings.blink_period_ms = period_ms;
  console_printf("blink: %lu ms (not saved, type save)\r\n", period_ms);
}

static void command_save(void)
{
  /* На время стирания сектора цикл стоит 1–2 с, автодизарм не сработает */
  if (motors_armed)
  {
    console_printf("save: disarm first\r\n");
    return;
  }
  /* Время записи здесь не измерить: пока стирается сектор, SysTick тоже стоит */
  console_printf("save: %s\r\n", settings_save() ? "ok" : "FLASH ERROR");
}

static void command_help(void)
{
  /* По строке за вызов: console_printf обрезает длинный текст */
  console_printf("help              this list\r\n");
  console_printf("status            all sensors at once\r\n");
  console_printf("imu               gyro/accel ID and reading\r\n");
  console_printf("baro              pressure and temperature\r\n");
  console_printf("osd               AT7456E presence\r\n");
  console_printf("power             battery, current, rssi, airspeed\r\n");
  console_printf("i2c               scan I2C1\r\n");
  console_printf("servo <1-6> <us>  set pulse, 0 = off (S1/S2 only when armed)\r\n");
  console_printf("outputs           set and measured pulse on S1-S6\r\n");
  console_printf("arm / disarm      motors S1/S2, max %u us, auto disarm %u s\r\n",
                 MOTOR_TEST_MAX_US, MOTOR_DISARM_TIMEOUT_MS / MS_PER_SECOND);
  console_printf("pinio <1|2> <0|1> PC15 Vbat switch / PB10 camera switch\r\n");
  console_printf("blink [ms]        show / set LED blink period, %u..%u\r\n",
                 BLINK_PERIOD_MIN_MS, BLINK_PERIOD_MAX_MS);
  console_printf("save              write settings to flash\r\n");
  console_printf("reboot            restart the board\r\n");
  console_printf("dfu               reboot to DFU bootloader\r\n");
}

static void execute(char *line)
{
  char command[WORD_SIZE] = "";
  char arg1[WORD_SIZE] = "";
  char arg2[WORD_SIZE] = "";

  sscanf(line, "%15s %15s %15s", command, arg1, arg2);

  if      (strcmp(command, "help") == 0)    { command_help(); }
  else if (strcmp(command, "status") == 0)  { command_status(); }
  else if (strcmp(command, "imu") == 0)     { command_imu(); }
  else if (strcmp(command, "baro") == 0)    { command_baro(); }
  else if (strcmp(command, "osd") == 0)     { command_osd(); }
  else if (strcmp(command, "power") == 0)   { command_power(); }
  else if (strcmp(command, "i2c") == 0)     { command_i2c_scan(); }
  else if (strcmp(command, "servo") == 0)   { command_servo(arg1, arg2); }
  else if (strcmp(command, "outputs") == 0) { command_outputs(); }
  else if (strcmp(command, "pinio") == 0)   { command_pinio(arg1, arg2); }
  else if (strcmp(command, "arm") == 0)
  {
    set_motors(MOTOR_STOP_US);
    motors_armed = true;
    console_printf("ARMED: S1/S2 = %u us. Props off!\r\n", MOTOR_STOP_US);
  }
  else if (strcmp(command, "disarm") == 0)  { disarm("command"); }
  else if (strcmp(command, "blink") == 0)   { command_blink(arg1); }
  else if (strcmp(command, "save") == 0)    { command_save(); }
  else if (strcmp(command, "reboot") == 0)  { NVIC_SystemReset(); }
  else if (strcmp(command, "dfu") == 0)
  {
    console_printf("Rebooting to DFU...\r\n");
    Bootloader_RequestDfu();
  }
  else
  {
    console_printf("unknown command '%s', type help\r\n", command);
  }
}

void bench_init(void)
{
  /* Светодиоды мигают по очереди: видно, что живы оба */
  HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LED_BLUE_GPIO_Port, LED_BLUE_Pin, GPIO_PIN_RESET);
  blink_ms = HAL_GetTick();
  settings_loaded = settings_load();

  /* Ошибки здесь не останавливают стенд: их покажут status и команды датчиков */
  (void)outputs_init();
  (void)power_init();
  /* После холодного включения первая попытка не проходит (проверено 23.10.2026):
     чип ещё не готов, когда МК уже стартовал. Сброс и перепрошивка этого не видят */
  for (imu_init_attempts = 1; imu_init_attempts <= IMU_INIT_MAX_ATTEMPTS; imu_init_attempts++)
  {
    imu_ready = bmi270_init();
    if (imu_ready)
    {
      break;
    }
    HAL_Delay(IMU_INIT_RETRY_DELAY_MS);
  }
  (void)spl06_init();
}

void bench_poll(void)
{
  char line[LINE_SIZE];
  uint32_t now_ms = HAL_GetTick();

  /* blink_ms = now, а не += период: после смены периода не догонять пропущенные мигания */
  if ((now_ms - blink_ms) >= settings.blink_period_ms)
  {
    blink_ms = now_ms;
    HAL_GPIO_TogglePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin);
    HAL_GPIO_TogglePin(LED_BLUE_GPIO_Port, LED_BLUE_Pin);
  }

  if (console_read_line(line, sizeof(line)))
  {
    last_command_ms = HAL_GetTick();
    execute(line);
  }

  if (motors_armed && ((HAL_GetTick() - last_command_ms) > MOTOR_DISARM_TIMEOUT_MS))
  {
    disarm("timeout");
  }

  console_poll();
}
