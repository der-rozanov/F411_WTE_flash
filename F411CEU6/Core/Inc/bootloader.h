/**
  * Переход в системный загрузчик STM32 (USB DFU) без кнопки BOOT.
  *
  * Bootloader_RequestDfu() ставит запрос (можно звать из прерывания USB),
  * Bootloader_Poll() в главном цикле записывает флаг в backup-регистр и
  * перезагружает МК, а Bootloader_CheckAndJump() в самом начале main()
  * видит флаг и прыгает в ROM-загрузчик.
  */
#ifndef __BOOTLOADER_H
#define __BOOTLOADER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Скорость "1200 baud touch": открытие COM-порта на ней = переход в DFU */
#define DFU_TOUCH_BAUD  1200u

void Bootloader_CheckAndJump(void);
void Bootloader_RequestDfu(void);
void Bootloader_Poll(void);

#ifdef __cplusplus
}
#endif

#endif /* __BOOTLOADER_H */
