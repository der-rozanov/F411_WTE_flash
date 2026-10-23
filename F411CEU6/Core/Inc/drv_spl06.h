/**
  * Барометр SPL06-001 на I2C1 (PB8/PB9), адрес 0x76 или 0x77 — ищется сам.
  */
#ifndef DRV_SPL06_H
#define DRV_SPL06_H

#include <stdbool.h>
#include <stdint.h>

bool spl06_init(void);
uint8_t spl06_address(void);  /* 7-битный адрес найденного чипа, 0 = не найден */
bool spl06_read(int32_t *pressure_pa, int32_t *temperature_cdeg);

#endif /* DRV_SPL06_H */
