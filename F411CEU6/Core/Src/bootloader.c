/**
  * Переход в системный загрузчик STM32F411 (USB DFU) по команде.
  * Описание интерфейса в bootloader.h.
  */
#include "main.h"
#include "usb_device.h"
#include "usbd_core.h"
#include "bootloader.h"

#define DFU_MAGIC            0xB007DF00u
#define SYSMEM_BASE          0x1FFF0000u  /* ROM-загрузчик STM32F411 */
#define DFU_REBOOT_DELAY_MS  50u          /* дать хосту дочитать ответ */

extern USBD_HandleTypeDef hUsbDeviceFS;

static volatile uint8_t dfuRequested;
static volatile uint32_t dfuRequestTick;

/* Backup-регистры RTC переживают программный сброс, запись требует DBP */
static void backup_access_enable(void)
{
  RCC->APB1ENR |= RCC_APB1ENR_PWREN;
  (void)RCC->APB1ENR;
  PWR->CR |= PWR_CR_DBP;
}

/* Вызывать первой строкой main(), до HAL_Init(): тактирование и периферия
   ещё в состоянии после сброса, как ожидает ROM-загрузчик. */
void Bootloader_CheckAndJump(void)
{
  static void (*sysmem_entry)(void);  /* static: не на стеке, который меняем */

  backup_access_enable();
  if (RTC->BKP0R != DFU_MAGIC)
  {
    return;
  }
  RTC->BKP0R = 0;

  __HAL_RCC_SYSCFG_CLK_ENABLE();
  __HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH();

  sysmem_entry = (void (*)(void))(*(volatile uint32_t *)(SYSMEM_BASE + 4u));
  __set_MSP(*(volatile uint32_t *)SYSMEM_BASE);
  sysmem_entry();

  while (1)
  {
  }
}

void Bootloader_RequestDfu(void)
{
  dfuRequestTick = HAL_GetTick();
  dfuRequested = 1;
}

void Bootloader_Poll(void)
{
  if (!dfuRequested || (HAL_GetTick() - dfuRequestTick) < DFU_REBOOT_DELAY_MS)
  {
    return;
  }

  backup_access_enable();
  RTC->BKP0R = DFU_MAGIC;

  /* Отключиться от шины, чтобы хост увидел уход CDC и пришёл DFU */
  USBD_Stop(&hUsbDeviceFS);
  HAL_Delay(10);
  NVIC_SystemReset();
}
