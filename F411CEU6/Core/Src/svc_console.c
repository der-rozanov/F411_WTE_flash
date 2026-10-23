/**
  * Консоль стенда поверх USB CDC: кольцевые буферы приёма и вывода.
  *
  * Приём: прерывание USB пишет в rx_buffer, главный цикл читает. Один писатель
  * и один читатель, поэтому хватает volatile-индексов без запрета прерываний.
  * Вывод: console_printf пишет в tx_buffer, console_poll отправляет кусками,
  * когда прошлая USB-передача закончилась. Если терминал не открыт и буфер
  * полон, новый вывод отбрасывается: стенд не должен зависать из-за консоли.
  */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "usbd_cdc_if.h"
#include "svc_console.h"

#define RX_BUFFER_SIZE     256u
#define TX_BUFFER_SIZE     2048u
#define TX_PACKET_SIZE     256u   /* сколько байт уходит одной USB-передачей */
#define PRINTF_LINE_SIZE   192u
#define INPUT_LINE_SIZE    64u

#define ASCII_BACKSPACE    0x08
#define ASCII_DELETE       0x7F   /* так Backspace шлёт PuTTY */

extern USBD_HandleTypeDef hUsbDeviceFS;

static uint8_t rx_buffer[RX_BUFFER_SIZE];
static volatile uint32_t rx_head;   /* пишет прерывание */
static volatile uint32_t rx_tail;   /* читает главный цикл */

static uint8_t tx_buffer[TX_BUFFER_SIZE];
static uint32_t tx_head;
static uint32_t tx_tail;
static uint8_t tx_packet[TX_PACKET_SIZE];  /* живёт до конца USB-передачи */

/* Строка копится здесь, а не у вызывающего: человек печатает по букве,
   и между вызовами console_read_line() буфер вызывающего живёт на стеке */
static char input_line[INPUT_LINE_SIZE];
static uint32_t line_length;

void console_receive(const uint8_t *data, uint32_t length)
{
  for (uint32_t i = 0; i < length; i++)
  {
    uint32_t next = (rx_head + 1u) % RX_BUFFER_SIZE;
    if (next == rx_tail)
    {
      return;  /* буфер полон: остаток пакета теряется */
    }
    rx_buffer[rx_head] = data[i];
    rx_head = next;
  }
}

static void console_write(const char *text, uint32_t length)
{
  for (uint32_t i = 0; i < length; i++)
  {
    uint32_t next = (tx_head + 1u) % TX_BUFFER_SIZE;
    if (next == tx_tail)
    {
      return;
    }
    tx_buffer[tx_head] = (uint8_t)text[i];
    tx_head = next;
  }
}

void console_printf(const char *format, ...)
{
  char text[PRINTF_LINE_SIZE];
  va_list args;

  va_start(args, format);
  int length = vsnprintf(text, sizeof(text), format, args);
  va_end(args);

  if (length < 0)
  {
    return;
  }
  if ((uint32_t)length >= sizeof(text))
  {
    length = sizeof(text) - 1u;  /* строка обрезана vsnprintf */
  }
  console_write(text, (uint32_t)length);
}

/* USB передаёт прошлый пакет, пока TxState не сброшен в прерывании */
static bool usb_is_ready_to_send(void)
{
  USBD_CDC_HandleTypeDef *cdc = (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
  return (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED) && (cdc != NULL) && (cdc->TxState == 0u);
}

void console_poll(void)
{
  if ((tx_head == tx_tail) || !usb_is_ready_to_send())
  {
    return;
  }

  uint16_t packet_length = 0;
  while ((tx_tail != tx_head) && (packet_length < TX_PACKET_SIZE))
  {
    tx_packet[packet_length++] = tx_buffer[tx_tail];
    tx_tail = (tx_tail + 1u) % TX_BUFFER_SIZE;
  }
  CDC_Transmit_FS(tx_packet, packet_length);
}

bool console_read_line(char *line, uint32_t line_size)
{
  while (rx_tail != rx_head)
  {
    char c = (char)rx_buffer[rx_tail];
    rx_tail = (rx_tail + 1u) % RX_BUFFER_SIZE;

    if ((c == '\r') || (c == '\n'))
    {
      if (line_length == 0u)
      {
        continue;  /* пустая строка или вторая половина "\r\n" */
      }
      input_line[line_length] = '\0';
      strncpy(line, input_line, line_size - 1u);
      line[line_size - 1u] = '\0';
      line_length = 0;
      console_write("\r\n", 2);
      return true;
    }
    if ((c == ASCII_BACKSPACE) || (c == ASCII_DELETE))
    {
      if (line_length > 0u)
      {
        line_length--;
        console_write("\b \b", 3);  /* стереть символ на экране терминала */
      }
      continue;
    }
    if (line_length < (sizeof(input_line) - 1u))
    {
      input_line[line_length++] = c;
      console_write(&c, 1);  /* эхо: терминалы обычно сами не печатают ввод */
    }
  }
  return false;
}
