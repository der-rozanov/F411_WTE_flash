/**
  * Константы платы Matek F411-WTE: делители, масштабы, опорное напряжение.
  * Источник: таргет INAV MATEKF411TE (target.h), см. Doc/Matek F411-WTE как плата разработки.md
  */
#ifndef BOARD_H
#define BOARD_H

/* АЦП: 12 бит, опора = питание МК 3,3 В */
#define ADC_REFERENCE_MV      3300u
#define ADC_FULL_SCALE        4095u

/* Батарея: делитель 1:11 перед PA4 (в INAV scale 1100) */
#define VBAT_DIVIDER          11u

/* Датчик тока: 25 мВ на выходе = 1 А (до 132 А) */
#define CURRENT_MV_PER_AMP    25u

/* Выходы S1–S6: таймеры тактируются 1 МГц, одно деление = 1 мкс */
#define OUTPUT_COUNT          6u
#define OUTPUT_PERIOD_US      20000u   /* 50 Гц, Period 19999 в CubeMX */

#endif /* BOARD_H */
