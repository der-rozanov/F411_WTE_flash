/**
  * Хранение настроек во flash STM32F411: сектор 7 (0x08060000, 128 КБ), последний.
  * Справочник: RM0383, раздел 3 (Flash memory interface).
  *
  * EEPROM у чипа нет. Сектор 7 исключён из области кода в STM32F411CEUX_FLASH.ld,
  * а flash_dfu.bat стирает только сектора прошивки, поэтому настройки переживают перепрошивку.
  *
  * Запись: стереть сектор, записать данные, последним записать MAGIC. Если питание
  * пропадёт посреди записи, MAGIC останется стёртым (0xFFFFFFFF) и при старте
  * возьмутся значения по умолчанию, а не мусор.
  *
  * Стирание 128 КБ занимает около 1 с (измерено 1,06 с), всё это время программа
  * стоит вместе с прерываниями (код и таблица векторов тоже во flash), HAL_GetTick()
  * эту секунду теряет. Поэтому запись только по команде save. Ресурс сектора — 10 000 стираний.
  */
#include "main.h"
#include "svc_settings.h"

#define SETTINGS_ADDRESS    0x08060000u
#define SETTINGS_SECTOR     FLASH_SECTOR_7
#define SETTINGS_MAGIC      0x5E770001u  /* меняется при несовместимом изменении settings_t */

typedef struct
{
  settings_t data;
  uint32_t magic;  /* последнее поле: пишется последним */
} stored_settings_t;

_Static_assert(sizeof(settings_t) % sizeof(uint32_t) == 0, "settings_t пишется во flash словами по 4 байта");

settings_t settings ={ .blink_period_ms = BLINK_PERIOD_DEFAULT_MS };

static const stored_settings_t *const stored = (const stored_settings_t *)SETTINGS_ADDRESS;

bool settings_blink_period_is_valid(uint32_t period_ms)
{
  return (period_ms >= BLINK_PERIOD_MIN_MS) && (period_ms <= BLINK_PERIOD_MAX_MS);
}

bool settings_load(void)
{
  if ((stored->magic != SETTINGS_MAGIC) || !settings_blink_period_is_valid(stored->data.blink_period_ms))
  {
    return false;
  }
  settings = stored->data;
  return true;
}

static bool erase_sector(void)
{
  FLASH_EraseInitTypeDef erase = {
    .TypeErase    = FLASH_TYPEERASE_SECTORS,
    .Sector       = SETTINGS_SECTOR,
    .NbSectors    = 1,
    .VoltageRange = FLASH_VOLTAGE_RANGE_3,  /* питание 3,3 В: запись словами по 32 бита */
  };
  uint32_t failed_sector = 0;

  return HAL_FLASHEx_Erase(&erase, &failed_sector) == HAL_OK;
}

static bool program_words(uint32_t address, const uint32_t *words, uint32_t count)
{
  for (uint32_t i = 0; i < count; i++)
  {
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address + i * sizeof(uint32_t), words[i]) != HAL_OK)
    {
      return false;
    }
  }
  return true;
}

bool settings_save(void)
{
  const uint32_t magic = SETTINGS_MAGIC;
  const uint32_t data_words = sizeof(settings_t) / sizeof(uint32_t);

  if (HAL_FLASH_Unlock() != HAL_OK)
  {
    return false;
  }
  /* Флаги ошибок от прошлых операций иначе сорвут следующую */
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                         FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

  bool ok = erase_sector()
         && program_words(SETTINGS_ADDRESS, (const uint32_t *)&settings, data_words)
         && program_words((uint32_t)&stored->magic, &magic, 1);

  HAL_FLASH_Lock();

  /* Прочитать обратно: записалось ровно то, что в RAM */
  return ok && (stored->magic == SETTINGS_MAGIC)
            && (stored->data.blink_period_ms == settings.blink_period_ms);
}
