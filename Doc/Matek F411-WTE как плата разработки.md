# Matek F411-WTE как плата разработки

22.10.2026

Полётный контроллер Matek F411-WTE (STM32F411CEU6) используется как плата разработки: проект STM32CubeIDE `F411CEU6`, прошивка только по USB DFU, без кнопки — по команде через USB. Ниже — как устроен переход в DFU и все найденные настройки платы для CubeMX.

Проект: `C:\Users\MAN-MADE\Desktop\F411 discovery\F411CEU6`. Официального PDF-мануала у Matek нет; распиновка взята из исходников INAV (таргет `MATEKF411TE`).

Онлайн-версия документа: https://claude.ai/artifact/GiTNLFXfSBm6kM8Cf8YbpK

## Загрузчик: как устроен переход в DFU

Своего загрузчика нет: используется заводской загрузчик STM32 в ПЗУ чипа (адрес `0x1FFF0000`). Его нельзя стереть, поэтому плату нельзя «окирпичить» своей прошивкой. Наша программа лежит во flash с `0x08000000` (на месте стёртого INAV) и умеет сама прыгнуть в этот загрузчик.

Войти в DFU можно тремя способами:

| Способ | Как | Когда работает |
| --- | --- | --- |
| Касание порта на 1200 бод | открыть COM-порт платы на 1200 бод со снятым DTR и закрыть (так делает `enter_dfu.ps1`) | наша прошивка работает |
| Команда `dfu` | набрать `dfu` + Enter в любом терминале на COM-порту; ответ `Rebooting to DFU...` | наша прошивка работает |
| Кнопка на плате (BOOT0) | зажать и переподключить USB | всегда, даже если прошивка зависла |

Последовательность программного перехода:

1. Обработчик USB CDC видит команду (1200 бод + снятый DTR или строку `dfu`) и вызывает `Bootloader_RequestDfu()`.
2. Через 50 мс `Bootloader_Poll()` в главном цикле пишет флаг `0xB007DF00` в backup-регистр `RTC->BKP0R`, отключает USB и делает программный сброс. Backup-регистр переживает сброс, очищается только при полном отключении питания.
3. После сброса первой строкой `main()`, до `HAL_Init()`, стоит `Bootloader_CheckAndJump()`. Она видит флаг, стирает его, переключает память на системную и прыгает по адресу из `0x1FFF0004`.
4. Плата появляется в системе как «STM32 BOOTLOADER» (VID 0483, PID DF11), и STM32CubeProgrammer её прошивает.

Касание срабатывает только при 1200 бод и снятом DTR, как в Arduino: обычный терминал на другой скорости плату не перезагрузит.

### Где лежат файлы

| Файл | Что в нём |
| --- | --- |
| `Core/Src/bootloader.c`, `Core/Inc/bootloader.h` | сам переход: флаг в BKP0R, сброс, прыжок в `0x1FFF0000`; константа `DFU_TOUCH_BAUD` = 1200 |
| `Core/Src/main.c` | вызов `Bootloader_CheckAndJump()` в `USER CODE 1` и `Bootloader_Poll()` в главном цикле |
| `USB_DEVICE/App/usbd_cdc_if.c` | разбор команды `dfu`, касание на 1200 бод, хранение настроек порта |
| `enter_dfu.ps1` | находит COM-порт платы (VID 0483, PID 5740) и делает касание на 1200 бод |
| `flash_dfu.bat` | вся прошивка: перевод в DFU, ожидание до 20 попыток, запись, проверка, запуск |
| `Flash DFU.launch` | кнопка External Tools в CubeIDE: сборка + `flash_dfu.bat` |

Все вставки в сгенерированные файлы стоят в секциях `USER CODE` и переживают перегенерацию CubeMX.

## Как прошивать

Кнопка на плате нужна только в первый раз или если прошивка зависла. Обычная перепрошивка без кнопки занимает около 4 секунд.

1. **Из CubeIDE (основной способ).** Один раз нажать F5 (Refresh) на проекте. Дальше: Run → External Tools → **Flash DFU**. Конфигурация сама соберёт проект и прошьёт плату; ход виден во вкладке Console.
2. **Двойной клик по `flash_dfu.bat`.** Прошивает последнюю сборку `Debug\F411CEU6.elf`, поэтому сначала Ctrl+B в IDE.
3. **Из командной строки** для любого файла `.elf`, `.hex` или `.bin`: `flash_dfu.bat путь\к\файлу.elf`.

Что делает скрипт: если плата уже в DFU — сразу прошивает; иначе вызывает `enter_dfu.ps1`, ждёт появления DFU (около 40 с), пишет через `STM32_Programmer_CLI -c port=USB1 -w <файл> -v -s 0x08000000` и запускает программу. Программатор ищется в STM32CubeProgrammer, затем внутри CubeIDE.

### Запасной путь: кнопка

Зажать кнопку на плате, переподключить USB, отпустить и запустить любой из способов выше. Нужно, если прошивка зависла, сломан USB или на плате чужая прошивка (например, INAV).

### Что не сломать в своём коде

- Вызов `Bootloader_CheckAndJump()` должен остаться первой строкой `main()`, до `HAL_Init()`.
- `Bootloader_Poll()` должен регулярно вызываться в главном цикле; цикл не должен надолго зависать в `HAL_Delay`.
- USB_DEVICE (CDC), HSE и SYS = No Debug в `.ioc` остаются включёнными.
- Перед прошивкой закрыть терминал на COM-порту платы, иначе скрипт не сможет его открыть.
- Если CubeProgrammer не видит плату в DFU, проверить драйвер: в диспетчере устройств должно быть «STM32 BOOTLOADER», а не WinUSB от Zadig.

## Тактирование

Кварц 8 МГц, ядро на 96 МГц, USB ровно 48 МГц. Те же делители PLL использует INAV для F411; работа USB на плате подтвердила частоту кварца. 100 МГц не используем: с ними ровно 48 МГц для USB не получить.

| Параметр CubeMX (Clock Configuration) | Значение |
| --- | --- |
| RCC → High Speed Clock (HSE) | Crystal/Ceramic Resonator (PH0, PH1) |
| HSE Input frequency | 8 МГц |
| PLL Source Mux | HSE |
| /M, ×N, /P, /Q | 8, 192, 2, 4 |
| System Clock Mux | PLLCLK |
| SYSCLK = HCLK | 96 МГц (AHB /1) |
| APB1 | /2 → 48 МГц; таймеры APB1 96 МГц |
| APB2 | /1 → 96 МГц; таймеры APB2 96 МГц |
| 48MHz clocks (USB) | 48 МГц |
| Flash latency | 3 WS (CubeMX ставит сам) |

Все таймеры выходов (TIM3, TIM4) и SPI2 сидят на APB1: таймеры тактируются от 96 МГц, SPI2 — от 48 МГц.

## Распиновка платы

Все пины взяты из таргета INAV `MATEKF411TE` (`target.h`, `target.c`). Метки — те, что заданы в `.ioc` проекта.

| Функция | Пин | Периферия / метка | Примечание |
| --- | --- | --- | --- |
| Светодиод синий (LED0) | PA14 | GPIO `LED_BLUE` | пин SWD, поэтому отладчика нет |
| Светодиод зелёный (LED1) | PA13 | GPIO `LED_GREEN` | пин SWD |
| USB | PA11 / PA12 | USB_OTG_FS | VCP: VID 0483, PID 5740 |
| Кварц | PH0 / PH1 | RCC HSE | 8 МГц |
| SPI2 SCK / MISO / MOSI | PB13 / PB14 / PB15 | SPI2 | общая шина гироскопа и OSD |
| CS гироскопа | PC13 | GPIO `IMU_CS` | ICM42688P (с марта 2023), раньше BMI270 |
| CS OSD | PB12 | GPIO `OSD_CS` | AT7456E |
| I2C1 SCL / SDA | PB8 / PB9 | I2C1 | барометр SPL06-001 + внешние компас/датчик скорости |
| Напряжение батареи | PA4 | ADC1_IN4 | V_bat = V_adc × 11 (scale 1100) |
| Ток | PA5 | ADC1_IN5 | I = V_adc / 25 мВ на ампер, до 132 А |
| RSSI (аналоговый) | PA6 | ADC1_IN6 | 0–3,3 В |
| Датчик скорости (AirS) | PA7 | ADC1_IN7 | 0–3,3 В, без делителя |
| S1 (мотор) | PB6 | TIM4_CH1 | |
| S2 (мотор) | PB7 | TIM4_CH2 | |
| S3 (серво) | PB0 | TIM3_CH3 | |
| S4 (серво) | PB1 | TIM3_CH4 | |
| S5 (серво) | PB4 | TIM3_CH1 | |
| S6 (серво) | PB5 | TIM3_CH2 | |
| UART1 TX1 / RX1 | PA9 / PA10 | USART1 | GPS, отладка |
| UART2 TX2 / RX2 | PA2 / PA3 | USART2 | приёмник; пятак SBUS идёт на PA3 через инвертор |
| Пищалка (BZ-) | PB2 | GPIO `BEEPER` | включается высоким уровнем |
| Питание Vbat на VTX/камеру (PINIO1) | PC15 | GPIO `VBAT_SW` | по умолчанию включено |
| Переключение камер C1/C2 (PINIO2) | PB10 | GPIO `CAM_SW` | по умолчанию камера 1 |
| Лента WS2812 (LED) | PA8 | TIM1_CH1 + DMA | не настроена |
| Софт-сериал ST1 / SR1 | PA0 / PA1 | TIM5 | свободные GPIO |
| Софт-сериал ST2 / SR2 | PA15 / PB3 | TIM2 | свободные GPIO |

Ориентация гироскопа в INAV: ICM42688P — `CW180_DEG_FLIP`, BMI270 — `CW270_DEG_FLIP` (оси чипа относительно стрелки на плате). Уровень «включено» для PINIO не проверен: предположение — Low = Vbat включён и выбрана камера 1.

## Настройки CubeMX по периферии

Две ловушки: CubeMX сам ставит I2C1 на PB6/PB7 (это выходы S1/S2), а TIM3_CH1/CH2 на PA6/PA7 (это входы АЦП). Их нужно перенести на PB8/PB9 и PB4/PB5: щёлкнуть по нужному пину и выбрать функцию.

| Блок | Настройка |
| --- | --- |
| SYS | Debug = No Debug (PA13/PA14 заняты светодиодами), Timebase = SysTick |
| USB_OTG_FS | Device_Only, Activate_VBUS выключен |
| USB_DEVICE | Class = Communication Device Class (Virtual Port Com) |
| SPI2 | Full-Duplex Master, Hardware NSS = Disable, 8 bit, MSB first, CPOL Low, CPHA 1 Edge, Prescaler /8 (6 Мбит/с: OSD максимум 10, гироскоп 24) |
| IMU_CS, OSD_CS | GPIO_Output, Push-Pull, начальный уровень High (оба чипа молчат после старта) |
| I2C1 | I2C, Fast Mode 400 кГц, пины PB8/PB9 |
| ADC1 | IN4–IN7; Scan, Continuous, DMA Continuous Requests = Enabled; 4 конверсии, ранги 1–4 = каналы 4, 5, 6, 7; Sampling Time 480 Cycles у каждого ранга |
| DMA для ADC1 | DMA2 Stream 0, Circular, Half Word / Half Word, инкремент только Memory |
| TIM4 (S1, S2) | CH1, CH2 = PWM Generation; Prescaler 95 (такт 1 мкс), Period 19999 (50 Гц) или 2499 (400 Гц для регуляторов) |
| TIM3 (S3–S6) | CH1–CH4 = PWM Generation, CH1/CH2 на PB4/PB5; Prescaler 95, Period 19999 |
| USART1 | Asynchronous, PA9/PA10, 115200 8N1 |
| USART2 | Asynchronous, PA2/PA3; CRSF: 420000 8N1; SBUS: 100000, 9 bit + Even, 2 stop |
| BEEPER, VBAT_SW, CAM_SW | GPIO_Output, Push-Pull, начальный уровень Low |
| Project Manager | Stack 0x800; Keep User Code; периферия парами .c/.h |

Работа с периферией в коде:

- **SPI:** опустить CS нужного чипа, `HAL_SPI_TransmitReceive`, поднять CS. Жёлтая подсветка SPI2 в CubeMX нормальна: аппаратный NSS недоступен, потому что PB12 и PB9 заняты.
- **АЦП:** буфер `uint16_t buf[4]` (DMA пишет полусловами), один раз `HAL_ADC_Start_DMA(&hadc1, (uint32_t *)buf, 4)`; дальше `buf[0]` батарея, `buf[1]` ток, `buf[2]` RSSI, `buf[3]` датчик скорости обновляются сами.
- **Выходы:** `HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1)`, затем `__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 1500)` — длительность в микросекундах, обычно 1000–2000.
- **Как проверить датчики:** ICM42688P — регистр `0x75` (WHO_AM_I) = `0x47`; BMI270 — регистр `0x00` = `0x24`; SPL06-001 — регистр `0x0D` = `0x10`, адрес на I2C 0x76 или 0x77 (уточнить сканом шины).
- **BMI270 по SPI** (если стоит он): после включения чип в режиме I2C, первое чтение по SPI — холостое, оно переключает его на SPI. И при каждом чтении первый принятый байт после адреса — пустой, данные идут со второго.

## Проверка .ioc и сгенерированного кода (22.10.2026)

Сверены `.ioc` и сгенерированные `main.c`, `gpio.c`, `spi.c`, `i2c.c`, `adc.c`, `dma.c`, `tim.c`, `usart.c`, `stm32f4xx_it.c`, `stm32f4xx_hal_conf.h` с распиновкой выше и с таргетом INAV `MATEKF411TE`. Проверено чтением кода, на плате датчики ещё не опрашивались.

Совпадает:

- Тактирование: HSE 8 МГц кварц (`RCC_HSE_ON`, не bypass), M/N/P/Q = 8/192/2/4, APB1 /2, APB2 /1, 3 WS; `HSE_VALUE` = 8000000.
- SPI2: PB13/14/15 AF5, Master, 8 бит, MSB, режим 0 (CPOL Low, CPHA 1 Edge), программный NSS, **/8 = 6 Мбит/с** (уже исправлено владельцем). Режим 0 поддерживают ICM42688P, BMI270 и AT7456E.
- CS гироскопа PC13 и CS OSD PB12 — выходы с начальным High, оба чипа молчат после старта. PC13/PC15 — пины домена backup (до 2 МГц, не источник тока), для CS и ключа этого хватает, скорость Low стоит верно.
- I2C1: PB8/PB9 AF4, open-drain, 400 кГц. Внутренние подтяжки выключены — рассчитываем на подтяжки на плате (проверится сканом шины).
- ADC1: PA4–PA7 = IN4–IN7, ранги 1–4 по порядку (батарея, ток, RSSI, скорость), 12 бит, Scan + Continuous, 480 тактов на канал, такт АЦП 24 МГц (предел 36). DMA2 Stream0 Channel 0, Circular, полуслова, обработчик `DMA2_Stream0_IRQHandler` есть.
- TIM4 (S1/S2): PB6/PB7 AF2, Prescaler 95, Period 19999 → 50 Гц, шаг 1 мкс. TIM3 (S3–S6): PB0/PB1/PB4/PB5 AF2, Prescaler 95, Period 19999 → 50 Гц, шаг 1 мкс.
- Выходы после старта: Pulse = 0 и ШИМ не запущен — линии в Low, регуляторы молчат.
- Пищалка PB2, ключи PC15/PB10 — начальный Low. Для пищалки это «выключено»: в INAV у таргета `BEEPER_INVERTED`, пищит от High.
- USART1/USART2 — PA9/PA10 и PA2/PA3, 115200 8N1 (под CRSF/SBUS скорость поменяется, когда дойдём до приёмника).
- Прерывание гироскопа (INT) в INAV для этой платы не разведено, EXTI не нужен.

Исправлено владельцем по итогам проверки:

- [x] **TIM3 → Prescaler** был 0 (S3–S6 шли на 4,8 кГц, 1500 в регистре сравнения давали 15,6 мкс). Поставлен 95, в `tim.c` теперь `htim3.Init.Prescaler = 95`. Перегенерация изменила только эту строку, секции `USER CODE` и вызовы загрузчика в `main.c` на месте.

Замечание на будущее, не ошибка: при непрерывном АЦП с DMA прерывание DMA2 Stream0 срабатывает примерно каждые 41 мкс (полбуфера и полный буфер), около 24 000 раз в секунду, и почти ничего не делает. Для стенда терпимо. Для полётной прошивки — снять в CubeMX галку NVIC → Code generation → Force DMA channels Interrupts и выключить DMA2 stream0 global interrupt либо замедлить АЦП.

## Источники

- [Mateksys — Flight Controller F411-WTE](https://www.mateksys.com/?portfolio=f411-wte): официальная страница, смена BMI270 на ICM42688P с марта 2023
- [INAV MATEKF411TE target.h](https://github.com/iNavFlight/inav/blob/master/src/main/target/MATEKF411TE/target.h): пины, каналы АЦП, масштабы, ориентация гироскопа
- [INAV MATEKF411TE target.c](https://github.com/iNavFlight/inav/blob/master/src/main/target/MATEKF411TE/target.c): таймеры выходов S1–S6
- [INAV cmake/stm32.cmake](https://github.com/iNavFlight/inav/blob/master/cmake/stm32.cmake): кварц по умолчанию 8 МГц
- [INAV system_stm32f4xx.c](https://github.com/iNavFlight/inav/blob/master/src/main/target/system_stm32f4xx.c): PLL для F411 (N=192, P=2, Q=4)
- [SoloGood F411-WTE manual (Manuals+)](https://manuals.plus/asin/B09YXSZ246): характеристики и схема платы
