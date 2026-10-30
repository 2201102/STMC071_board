/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "app_usbx_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ws2812.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

TIM_HandleTypeDef htim1;
DMA_HandleTypeDef hdma_tim1_ch1;

PCD_HandleTypeDef hpcd_USB_DRD_FS;

/* USER CODE BEGIN PV */
#define RX_BUF_SIZE 64
#define LINE_BUF_SIZE    64             /* 終端の\0込み */
static uint8_t  rx_buf[RX_BUF_SIZE];
static uint32_t rx_len = 0;
static char     line_buf[LINE_BUF_SIZE];
static uint16_t line_len = 0;
static uint8_t  prev_cr = 0;

#define NEO_LEDS         6
#define BTN_DEBOUNCE_MS   30U      /* チャタリング除去 */
#define BTN_LONG_MS       600U     /* この時間以上押し続けると長押し */
#define COLOR_STEP_MS     20U      /* 長押し中、色を進める間隔 */
#define COLOR_STEP        2U       /* 1回で進める色相(0..255)。全周 約2.5秒 */

static uint32_t frame_ms   = 20;     /* speedで変わる（100 / speed） */
static uint8_t  speed_val  = 5;
#define BRIGHT_DEFAULT  32U          /* 輝度の初期値 */
#define BRIGHT_MAX      128U         /* 輝度の上限 */
static uint8_t  bright_val = BRIGHT_DEFAULT;
static uint32_t reset_at   = 0;

static uint8_t  auto_demo = 0;
static uint32_t auto_interval_ms = 5000;
static uint32_t auto_next_at = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_TIM1_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

//neopixel

typedef enum {
  PAT_SOLID, PAT_BLINK, PAT_BREATH,
  PAT_RAINBOW, PAT_CHASE, PAT_KNIGHT, PAT_TWINKLE, PAT_HUE,
  PAT_MANUAL,                           /* set / fill 用（ボタン巡回では飛ばす） */
  PAT_COUNT
} neo_pat_t;

static const char *pat_names[PAT_COUNT] = {
  "solid", "blink", "breath", "rainbow", "chase", "knight", "twinkle", "hue", "manual"
};

static neo_pat_t pat       = PAT_RAINBOW;
static uint8_t   col_r = 255, col_g = 0, col_b = 0;   /* 単色系パターンの色 */
static uint8_t   col_hue = 0;                         /* 長押しで変える色相（0=赤） */
static uint32_t  frame = 0;
static uint8_t   tw[NEO_LEDS];          /* twinkle用の明るさ */
static uint32_t  rnd = 2463534242u;

static uint32_t xorshift(void)
{
  rnd ^= rnd << 13; rnd ^= rnd >> 17; rnd ^= rnd << 5;
  return rnd;
}

/* 輝度は WS2812_Show() 側で掛かる */
static void put(uint32_t i, uint8_t r, uint8_t g, uint8_t b)
{
  WS2812_SetPixel(i, r, g, b);
}
/* 単色(col_*) × level(0..255) */
static void put_level(uint32_t i, uint8_t level)
{
  uint32_t l = (uint32_t)level + 1;
  put(i, (col_r * l) >> 8, (col_g * l) >> 8, (col_b * l) >> 8);
}

/* 色相 0..255 → RGB（彩度・明度は最大） */
static void hue2rgb(uint8_t h, uint8_t *r, uint8_t *g, uint8_t *b)
{
  uint8_t region = h / 43;
  uint8_t rem = (uint8_t)((h - region * 43) * 6);
  switch (region) {
  case 0:  *r = 255;       *g = rem;       *b = 0;         break;
  case 1:  *r = 255 - rem; *g = 255;       *b = 0;         break;
  case 2:  *r = 0;         *g = 255;       *b = rem;       break;
  case 3:  *r = 0;         *g = 255 - rem; *b = 255;       break;
  case 4:  *r = rem;       *g = 0;         *b = 255;       break;
  default: *r = 255;       *g = 0;         *b = 255 - rem; break;
  }
}

void neo_set_pattern(neo_pat_t p)
{
  pat = p;
  frame = 0;
  memset(tw, 0, sizeof(tw));
}

/* メインループから毎回呼ぶ。frame_ms(既定20ms)ごとに1フレーム進める */
void neo_update(void)
{
  static uint32_t last = 0;
  if (!WS2812_IsReady()) return;                    /* 送信中は飛ばす */
  if ((HAL_GetTick() - last) < frame_ms) return;
  last = HAL_GetTick();

  uint32_t t = frame++;

  switch (pat) {
  case PAT_MANUAL:                          /* set / fill で指定した色を保持 */
    break;

  case PAT_SOLID:
    for (uint32_t i = 0; i < NEO_LEDS; i++) put_level(i, 255);
    break;

  case PAT_BLINK: {                         /* 0.5秒ごとに点滅 */
    uint8_t lv = ((t / 25) & 1) ? 0 : 255;
    for (uint32_t i = 0; i < NEO_LEDS; i++) put_level(i, lv);
    break;
  }

  case PAT_BREATH: {                        /* 呼吸（約2秒周期） */
    uint32_t ph = (t * 6) % 512;
    uint32_t v  = (ph < 256) ? ph : 511 - ph;   /* 三角波 */
    v = (v * v) >> 8;                           /* 簡易ガンマ */
    for (uint32_t i = 0; i < NEO_LEDS; i++) put_level(i, (uint8_t)v);
    break;
  }

  case PAT_RAINBOW:                         /* 虹が流れる */
    for (uint32_t i = 0; i < NEO_LEDS; i++) {
      uint8_t r, g, b;
      hue2rgb((uint8_t)(t * 2 + (i * 256) / NEO_LEDS), &r, &g, &b);
      put(i, r, g, b);
    }
    break;

  case PAT_CHASE: {                         /* 1個が走る */
    uint32_t pos = (t / 6) % NEO_LEDS;
    for (uint32_t i = 0; i < NEO_LEDS; i++) put_level(i, (i == pos) ? 255 : 0);
    break;
  }

  case PAT_KNIGHT: {                        /* 往復＋尾を引く */
    const uint32_t period = 2 * (NEO_LEDS - 1);
    uint32_t s   = (t / 3) % period;
    uint32_t pos = (s < NEO_LEDS) ? s : period - s;
    for (uint32_t i = 0; i < NEO_LEDS; i++) {
      uint32_t d = (i > pos) ? i - pos : pos - i;
      uint8_t lv = (d == 0) ? 255 : (d == 1) ? 90 : (d == 2) ? 25 : 0;
      put_level(i, lv);
    }
    break;
  }

  case PAT_HUE: {                                   /* 全体が同じ色でゆっくり色相変化 */
	  uint8_t r, g, b;
	  hue2rgb((uint8_t)t, &r, &g, &b);
	  for (uint32_t i = 0; i < NEO_LEDS; i++) put(i, r, g, b);
	  break;
	}

  case PAT_TWINKLE:                         /* ランダムにキラッと光って消える */
    for (uint32_t i = 0; i < NEO_LEDS; i++) tw[i] = (tw[i] > 6) ? tw[i] - 6 : 0;
    if ((xorshift() & 7) == 0) tw[xorshift() % NEO_LEDS] = 255;
    for (uint32_t i = 0; i < NEO_LEDS; i++) put_level(i, tw[i]);
    break;

  default:
    break;
  }

  WS2812_Show();
}

static void cdc_puts(const char *s)
{
  USBD_CDC_ACM_Write((const uint8_t *)s, strlen(s));
}

static void neo_next_pattern(void);        /* 前方宣言 */

#define MAX_ARGS 6

static void cdc_printf(const char *fmt, ...)
{
  char tmp[96];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  if (n > 0) {
    if (n >= (int)sizeof(tmp)) n = sizeof(tmp) - 1;
    USBD_CDC_ACM_Write((const uint8_t *)tmp, (uint32_t)n);
  }
}

/* 空白で区切って argv を作る（line を直接書き換える） */
static int tokenize(char *s, char *argv[])
{
  int argc = 0;
  while (*s && argc < MAX_ARGS) {
    while (*s == ' ') *s++ = '\0';
    if (*s == '\0') break;
    argv[argc++] = s;
    while (*s && *s != ' ') s++;
  }
  return argc;
}

/* 範囲チェック付きの整数パース */
static int parse_int(const char *s, int lo, int hi, int *out)
{
  char *end;
  long v = strtol(s, &end, 10);
  if (end == s || *end != '\0' || v < lo || v > hi) return 0;
  *out = (int)v;
  return 1;
}

static void bad_args(void) { cdc_puts("bad args (see help)\r\n"); }

/* ---- ハンドラ ---- */
static void cmd_list(int argc, char *argv[])
{
  (void)argc; (void)argv;
  for (int i = 0; i < PAT_COUNT; i++) {
    cdc_puts(pat_names[i]);
    cdc_puts(i < PAT_COUNT - 1 ? " " : "\r\n");
  }
}

static void cmd_pat(int argc, char *argv[])
{
  if (argc != 2) { bad_args(); return; }
  for (int i = 0; i < PAT_COUNT; i++) {
    if (strcmp(argv[1], pat_names[i]) == 0) {
      neo_set_pattern((neo_pat_t)i);
      cdc_puts("ok\r\n");
      return;
    }
  }
  cdc_puts("unknown pattern\r\n");
}

static void cmd_next(int argc, char *argv[])
{
  (void)argc; (void)argv;
  neo_next_pattern();
}

static void cmd_bright(int argc, char *argv[])
{
  int v;
  if (argc != 2 || !parse_int(argv[1], 0, BRIGHT_MAX, &v)) { bad_args(); return; }
  bright_val = (uint8_t)v;
  WS2812_SetBrightness(bright_val);
  cdc_puts("ok\r\n");
}

static void cmd_speed(int argc, char *argv[])
{
  int v;
  if (argc != 2 || !parse_int(argv[1], 1, 10, &v)) { bad_args(); return; }
  speed_val = (uint8_t)v;
  frame_ms  = 100U / (uint32_t)v;           /* 1→100ms, 5→20ms, 10→10ms */
  cdc_puts("ok\r\n");
}

static void cmd_color(int argc, char *argv[])
{
  int r, g, b;
  if (argc != 4 || !parse_int(argv[1], 0, 255, &r) ||
      !parse_int(argv[2], 0, 255, &g) || !parse_int(argv[3], 0, 255, &b)) {
    bad_args(); return;
  }
  col_r = (uint8_t)r; col_g = (uint8_t)g; col_b = (uint8_t)b;
  cdc_puts("ok\r\n");
}

static void cmd_set(int argc, char *argv[])
{
  int idx, r, g, b;
  if (argc != 5 || !parse_int(argv[1], 0, WS2812_LED_COUNT - 1, &idx) ||
      !parse_int(argv[2], 0, 255, &r) || !parse_int(argv[3], 0, 255, &g) ||
      !parse_int(argv[4], 0, 255, &b)) {
    bad_args(); return;
  }
  if (pat != PAT_MANUAL) neo_set_pattern(PAT_MANUAL);   /* 直前のフレームを保持 */
  WS2812_SetPixel((uint32_t)idx, (uint8_t)r, (uint8_t)g, (uint8_t)b);
  cdc_puts("ok\r\n");
}

static void cmd_fill(int argc, char *argv[])
{
  int r, g, b;
  if (argc != 4 || !parse_int(argv[1], 0, 255, &r) ||
      !parse_int(argv[2], 0, 255, &g) || !parse_int(argv[3], 0, 255, &b)) {
    bad_args(); return;
  }
  if (pat != PAT_MANUAL) neo_set_pattern(PAT_MANUAL);
  WS2812_SetAll((uint8_t)r, (uint8_t)g, (uint8_t)b);
  cdc_puts("ok\r\n");
}

static void cmd_status(int argc, char *argv[])
{
  (void)argc; (void)argv;
  cdc_printf(
      "pat=%s bright=%u speed=%u auto=%u interval=%lu color=%u,%u,%u\r\n",
      pat_names[pat],
      (unsigned)bright_val,
      (unsigned)speed_val,
      (unsigned)auto_demo,
      (unsigned long)(auto_interval_ms / 1000),
      (unsigned)col_r,
      (unsigned)col_g,
      (unsigned)col_b);
}

static void cmd_uptime(int argc, char *argv[])
{
  (void)argc; (void)argv;
  uint32_t s = HAL_GetTick() / 1000U;
  cdc_printf("%lu:%02lu:%02lu\r\n", (unsigned long)(s / 3600U),
             (unsigned long)((s / 60U) % 60U), (unsigned long)(s % 60U));
}

static void cmd_auto(int argc, char *argv[])
{
    if (argc != 2)
    {
        bad_args();
        return;
    }

    if (strcmp(argv[1], "on") == 0)
    {
        auto_demo = 1;
        auto_next_at = HAL_GetTick() + auto_interval_ms;
        cdc_puts("auto on\r\n");
    }
    else if (strcmp(argv[1], "off") == 0)
    {
        auto_demo = 0;
        cdc_puts("auto off\r\n");
    }
    else
    {
        bad_args();
    }
}

static void cmd_interval(int argc, char *argv[])
{
    int v;

    if (argc != 2 ||
        !parse_int(argv[1], 1, 3600, &v))
    {
        bad_args();
        return;
    }

    auto_interval_ms = (uint32_t)v * 1000U;

    cdc_puts("ok\r\n");
}



static void cmd_reset(int argc, char *argv[])
{
  (void)argc; (void)argv;
  cdc_puts("resetting...\r\n");
  reset_at = HAL_GetTick() + 100U;           /* 返信を送り切ってからリセット */
  if (reset_at == 0U) reset_at = 1U;
}

/* ---- コマンドテーブル ---- */
typedef struct {
  const char *name;
  const char *usage;
  void (*fn)(int argc, char *argv[]);
} cmd_t;

static void cmd_help(int argc, char *argv[]);

static const cmd_t cmds[] = {
  { "help",   "",                   cmd_help   },
  { "list",   "",                   cmd_list   },
  { "pat",    "<name>",             cmd_pat    },
  { "next",   "",                   cmd_next   },
  { "auto",     "<on|off>",  cmd_auto     },
  { "interval", "<sec>",     cmd_interval },
  { "bright", "<0-128>",            cmd_bright },
  { "speed",  "<1-10>",             cmd_speed  },
  { "color",  "<r> <g> <b>",        cmd_color  },
  { "set",    "<idx> <r> <g> <b>",  cmd_set    },
  { "fill",   "<r> <g> <b>",        cmd_fill   },
  { "status", "",                   cmd_status },
  { "uptime", "",                   cmd_uptime },
  { "reset",  "",                   cmd_reset  },
};
#define CMD_COUNT  (sizeof(cmds) / sizeof(cmds[0]))

static void cmd_help(int argc, char *argv[])
{
  (void)argc; (void)argv;
  for (size_t i = 0; i < CMD_COUNT; i++) {
    cdc_printf("%s %s\r\n", cmds[i].name, cmds[i].usage);
  }
}

/* 1行確定時に呼ばれる */
static void on_line(char *line)
{
  char *argv[MAX_ARGS];
  int argc = tokenize(line, argv);
  if (argc == 0) return;

  for (size_t i = 0; i < CMD_COUNT; i++) {
    if (strcmp(argv[0], cmds[i].name) == 0) {
      cmds[i].fn(argc, argv);
      return;
    }
  }
  cdc_puts("unknown command (try: help)\r\n");
}

/* メインループから呼ぶ。resetコマンドの実行待ち */
static void reset_task(void)
{
  if (reset_at != 0U && (int32_t)(HAL_GetTick() - reset_at) >= 0) {
    NVIC_SystemReset();
  }
}

static void rx_byte(uint8_t c)
{
  uint8_t was_cr = prev_cr;
  prev_cr = (c == '\r');

  switch (c) {
  case '\r':
  case '\n':
    if (c == '\n' && was_cr) break;          /* CRLFの\nは読み飛ばす */
    line_buf[line_len] = '\0';
    on_line(line_buf);
    line_len = 0;
    break;

  case 0x08:                                 /* Backspace / DEL */
  case 0x7F:
    if (line_len > 0) line_len--;            /* バッファからだけ削除 */
    break;

  default:
    if (c >= 0x20 && c < 0x7F && line_len < LINE_BUF_SIZE - 1) {
      line_buf[line_len++] = (char)c;
    }
    break;
  }
}

static void neo_next_pattern(void)
{
  uint32_t n = (pat + 1) % PAT_COUNT;
  if (n == PAT_MANUAL) n = (n + 1) % PAT_COUNT;
  neo_set_pattern((neo_pat_t)n);
  cdc_puts(pat_names[pat]);
  cdc_puts("\r\n");
}

/* color の影響を受けるパターンか */
static uint8_t pat_uses_color(neo_pat_t p)
{
  return (p == PAT_RAINBOW || p == PAT_HUE || p == PAT_MANUAL) ? 0U : 1U;
}

/* メインループから毎回呼ぶ。
 *  短押し(離した時) : 次のパターン
 *  長押し(押している間): 色相が連続して変わる。離すと現在の色を表示 */
static void button_task(void)
{
  static GPIO_PinState raw_prev = GPIO_PIN_SET;   /* 押下でLow(プルアップ) */
  static GPIO_PinState stable   = GPIO_PIN_SET;
  static uint32_t raw_changed = 0;
  static uint32_t press_start = 0;
  static uint32_t last_step   = 0;
  static uint8_t  long_active = 0;

  uint32_t now = HAL_GetTick();
  GPIO_PinState raw = HAL_GPIO_ReadPin(button_GPIO_Port, button_Pin);

  /* チャタリング除去：一定時間変化しなければ確定 */
  if (raw != raw_prev) {
    raw_prev = raw;
    raw_changed = now;
  }
  if (raw != stable && (now - raw_changed) >= BTN_DEBOUNCE_MS) {
    stable = raw;
    if (stable == GPIO_PIN_RESET) {               /* 押した */
      press_start = now;
      long_active = 0;
    } else {                                      /* 離した */
      if (long_active) {
        cdc_printf("color=%u,%u,%u\r\n",
                   (unsigned)col_r, (unsigned)col_g, (unsigned)col_b);
      } else {
        neo_next_pattern();                       /* 短押し */
      }
      long_active = 0;
    }
  }

  /* 押している間 */
  if (stable == GPIO_PIN_RESET) {
    if (!long_active && (now - press_start) >= BTN_LONG_MS) {
      long_active = 1;
      last_step = now;
      if (!pat_uses_color(pat)) {
        neo_set_pattern(PAT_SOLID);               /* 色の変化が見えるパターンへ */
      }
    }
    if (long_active && (now - last_step) >= COLOR_STEP_MS) {
      last_step = now;
      col_hue = (uint8_t)(col_hue + COLOR_STEP);
      hue2rgb(col_hue, &col_r, &col_g, &col_b);
    }
  }
}

static void auto_demo_task(void)
{
    if (!auto_demo)
        return;

    uint32_t now = HAL_GetTick();

    if ((int32_t)(now - auto_next_at) >= 0)
    {
        auto_next_at = now + auto_interval_ms;
        neo_next_pattern();
    }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USBX_Device_Init();
  MX_TIM1_Init();
  /* USER CODE BEGIN 2 */
  MX_USB_PCD_Init();
  WS2812_SetBrightness(bright_val);
  neo_set_pattern(PAT_RAINBOW);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	ux_device_stack_tasks_run();

	USBD_CDC_ACM_Receive(rx_buf, RX_BUF_SIZE, &rx_len);
	for (uint32_t i = 0; i < rx_len; i++) {
	  rx_byte(rx_buf[i]);
	}
	rx_len = 0;

	USBD_CDC_ACM_TxTask();
	button_task();
	auto_demo_task();
	neo_update();
	reset_task();

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_CRSInitTypeDef RCC_CRSInitStruct = {0};

  __HAL_FLASH_SET_LATENCY(FLASH_LATENCY_1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_HSI48;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;

  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enable the CRS APB clock
  */
  __HAL_RCC_CRS_CLK_ENABLE();

  /** Configures CRS
  */
  RCC_CRSInitStruct.Prescaler = RCC_CRS_SYNC_DIV1;
  RCC_CRSInitStruct.Source = RCC_CRS_SYNC_SOURCE_USB;
  RCC_CRSInitStruct.Polarity = RCC_CRS_SYNC_POLARITY_RISING;
  RCC_CRSInitStruct.ReloadValue = __HAL_RCC_CRS_RELOADVALUE_CALCULATE(48000000,1000);
  RCC_CRSInitStruct.ErrorLimitValue = 34;
  RCC_CRSInitStruct.HSI48CalibrationValue = 32;

  HAL_RCCEx_CRSConfig(&RCC_CRSInitStruct);
}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 0;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 59;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.BreakFilter = 0;
  sBreakDeadTimeConfig.BreakAFMode = TIM_BREAK_AFMODE_INPUT;
  sBreakDeadTimeConfig.Break2State = TIM_BREAK2_DISABLE;
  sBreakDeadTimeConfig.Break2Polarity = TIM_BREAK2POLARITY_HIGH;
  sBreakDeadTimeConfig.Break2Filter = 0;
  sBreakDeadTimeConfig.Break2AFMode = TIM_BREAK_AFMODE_INPUT;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */
  HAL_TIM_MspPostInit(&htim1);

}

/**
  * @brief USB Initialization Function
  * @param None
  * @retval None
  */
void MX_USB_PCD_Init(void)
{

  /* USER CODE BEGIN USB_Init 0 */

  /* USER CODE END USB_Init 0 */

  /* USER CODE BEGIN USB_Init 1 */

  /* USER CODE END USB_Init 1 */
  hpcd_USB_DRD_FS.Instance = USB_DRD_FS;
  hpcd_USB_DRD_FS.Init.dev_endpoints = 8;
  hpcd_USB_DRD_FS.Init.speed = USBD_FS_SPEED;
  hpcd_USB_DRD_FS.Init.phy_itface = PCD_PHY_EMBEDDED;
  hpcd_USB_DRD_FS.Init.Sof_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.low_power_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.lpm_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.battery_charging_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.vbus_sensing_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.bulk_doublebuffer_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.iso_singlebuffer_enable = DISABLE;
  if (HAL_PCD_Init(&hpcd_USB_DRD_FS) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB_Init 2 */
  HAL_PCDEx_PMAConfig(&hpcd_USB_DRD_FS , 0x00 , PCD_SNG_BUF, 0x20);//EP0 OUT
  HAL_PCDEx_PMAConfig(&hpcd_USB_DRD_FS , 0x80 , PCD_SNG_BUF, 0x60);//EP0 IN
  HAL_PCDEx_PMAConfig(&hpcd_USB_DRD_FS , 0x81 , PCD_SNG_BUF, 0xA0);//EP1 IN
  HAL_PCDEx_PMAConfig(&hpcd_USB_DRD_FS , 0x82 , PCD_SNG_BUF, 0xE0);//EP2 IN
  HAL_PCDEx_PMAConfig(&hpcd_USB_DRD_FS , 0x03 , PCD_SNG_BUF, 0xF0);//EP3 OUT
  _ux_dcd_stm32_initialize((ULONG)NULL, (ULONG)&hpcd_USB_DRD_FS);
  HAL_PCD_Start(&hpcd_USB_DRD_FS);
  /* USER CODE END USB_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin : button_Pin */
  GPIO_InitStruct.Pin = button_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(button_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
