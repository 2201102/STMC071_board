/**
  ******************************************************************************
  * @file    ws2812.c
  * @brief   WS2812 / NeoPixel driver using TIM1_CH1 PWM output + DMA.
  ******************************************************************************
  */
#include "ws2812.h"

/* --- Timing -----------------------------------------------------------
 * TIM1 counts 0..11 (Period = 11) at 9.6 MHz -> 1.25 us per PWM period.
 * CCR1 value = high time of that period, in the same tick units.
 *   Logical "0": ~0.42 us high  -> CCR = 4
 *   Logical "1": ~0.83 us high  -> CCR = 8
 * These fall well inside the WS2812B tolerance windows.
 * ------------------------------------------------------------------- */
#define WS2812_DUTY_0          17U
#define WS2812_DUTY_1          34U

#define WS2812_BITS_PER_LED    24U
/* >=50us of continuous "low" latches the frame; 60 slots * 1.25us = 75us */
#define WS2812_RESET_SLOTS     240U
#define WS2812_BUF_LEN         (WS2812_LED_COUNT * WS2812_BITS_PER_LED + WS2812_RESET_SLOTS)

extern TIM_HandleTypeDef htim1;

static uint16_t ws2812_dma_buffer[WS2812_BUF_LEN];
static uint8_t  ws2812_pixels[WS2812_LED_COUNT][3]; /* stored as {R, G, B} */
static volatile uint8_t ws2812_tx_busy = 0U;
static uint8_t  ws2812_brightness = 255U; /* 0..255, applied at Show() time */

/* Scale an 8-bit color channel by the global brightness (0..255) */
static inline uint8_t WS2812_ApplyBrightness(uint8_t value)
{
  return (uint8_t)(((uint16_t)value * ws2812_brightness) / 255U);
}

void WS2812_SetBrightness(uint8_t brightness)
{
  ws2812_brightness = brightness;
}

/* Write the 8 PWM duty values (MSB first) that encode one color byte */
static void WS2812_EncodeByte(uint8_t byte, uint16_t *dst)
{
  for (uint8_t bit = 0U; bit < 8U; bit++)
  {
    dst[bit] = (byte & (0x80U >> bit)) ? WS2812_DUTY_1 : WS2812_DUTY_0;
  }
}

void WS2812_Init(void)
{
  WS2812_Clear();
}

void WS2812_SetPixel(uint32_t index, uint8_t r, uint8_t g, uint8_t b)
{
  if (index >= WS2812_LED_COUNT)
  {
    return;
  }
  ws2812_pixels[index][0] = r;
  ws2812_pixels[index][1] = g;
  ws2812_pixels[index][2] = b;
}

void WS2812_SetAll(uint8_t r, uint8_t g, uint8_t b)
{
  for (uint32_t i = 0U; i < WS2812_LED_COUNT; i++)
  {
    WS2812_SetPixel(i, r, g, b);
  }
}

void WS2812_Clear(void)
{
  WS2812_SetAll(0U, 0U, 0U);
}

uint8_t WS2812_IsReady(void)
{
  return (ws2812_tx_busy == 0U) ? 1U : 0U;
}

void WS2812_Show(void)
{
  uint32_t idx = 0U;

  /* Wait for any previous frame to finish shifting out */
  while (ws2812_tx_busy)
  {
  }

  for (uint32_t led = 0U; led < WS2812_LED_COUNT; led++)
  {
    /* WS2812 expects Green, Red, Blue transmission order */
    WS2812_EncodeByte(WS2812_ApplyBrightness(ws2812_pixels[led][1]), &ws2812_dma_buffer[idx]); idx += 8U;
    WS2812_EncodeByte(WS2812_ApplyBrightness(ws2812_pixels[led][0]), &ws2812_dma_buffer[idx]); idx += 8U;
    WS2812_EncodeByte(WS2812_ApplyBrightness(ws2812_pixels[led][2]), &ws2812_dma_buffer[idx]); idx += 8U;
  }

  /* Reset/latch pulse: hold the line low */
  for (uint32_t i = 0U; i < WS2812_RESET_SLOTS; i++)
  {
    ws2812_dma_buffer[idx++] = 0U;
  }

  ws2812_tx_busy = 1U;
  HAL_TIM_PWM_Start_DMA(&htim1, TIM_CHANNEL_1, (uint32_t *)ws2812_dma_buffer, WS2812_BUF_LEN);
}

/* HAL callback: fired once the whole buffer has been shifted out via DMA */
void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM1)
  {
    HAL_TIM_PWM_Stop_DMA(htim, TIM_CHANNEL_1);
    ws2812_tx_busy = 0U;
  }
}
