/**
  * Микросхема OSD AT7456E (аналог MAX7456) на SPI2, CS = PB12.
  * Пока только проверка, что чип отвечает.
  */
#ifndef DRV_AT7456_H
#define DRV_AT7456_H

#include <stdbool.h>
#include <stdint.h>

bool at7456_read_osdm(uint8_t *value);
bool at7456_is_present(void);

#endif /* DRV_AT7456_H */
