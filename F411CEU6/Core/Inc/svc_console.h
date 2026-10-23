/**
  * Консоль стенда поверх USB CDC (виртуальный COM-порт).
  *
  * console_receive() зовётся из прерывания USB и только кладёт байты в буфер.
  * console_poll() в главном цикле отправляет накопленный вывод в USB.
  * console_read_line() в главном цикле собирает строку с эхом и Backspace.
  */
#ifndef SVC_CONSOLE_H
#define SVC_CONSOLE_H

#include <stdbool.h>
#include <stdint.h>

void console_receive(const uint8_t *data, uint32_t length);
bool console_read_line(char *line, uint32_t line_size);
void console_printf(const char *format, ...) __attribute__((format(printf, 1, 2)));
void console_poll(void);

#endif /* SVC_CONSOLE_H */
