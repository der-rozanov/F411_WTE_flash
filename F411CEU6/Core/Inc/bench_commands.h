/**
  * Испытательный стенд: команды USB-консоли, проверка всех узлов платы.
  * bench_init() — один раз после MX_*_Init(), bench_poll() — в каждом проходе главного цикла.
  */
#ifndef BENCH_COMMANDS_H
#define BENCH_COMMANDS_H

void bench_init(void);
void bench_poll(void);

#endif /* BENCH_COMMANDS_H */
