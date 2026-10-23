/**
  * Настройки стенда, которые переживают выключение питания и перепрошивку.
  *
  * settings_load() — один раз при старте: читает flash или берёт значения по умолчанию.
  * Команды меняют поля settings в RAM, settings_save() записывает их во flash.
  */
#ifndef SVC_SETTINGS_H
#define SVC_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#define BLINK_PERIOD_MIN_MS      50u
#define BLINK_PERIOD_MAX_MS      5000u
#define BLINK_PERIOD_DEFAULT_MS  500u

typedef struct
{
  uint32_t blink_period_ms;  /* полупериод мигания светодиодов */
} settings_t;

extern settings_t settings;

bool settings_load(void);   /* false = во flash ничего нет, взяты значения по умолчанию */
bool settings_save(void);
bool settings_blink_period_is_valid(uint32_t period_ms);

#endif /* SVC_SETTINGS_H */
