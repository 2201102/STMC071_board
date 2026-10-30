/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    ux_device_cdc_acm.c
  * @author  MCD Application Team
  * @brief   USBX Device applicative file
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
#include "ux_device_cdc_acm.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

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
/* USER CODE BEGIN PV */
UX_SLAVE_CLASS_CDC_ACM  *cdc_acm=NULL;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  USBD_CDC_ACM_Activate
  *         This function is called when insertion of a CDC ACM device.
  * @param  cdc_acm_instance: Pointer to the cdc acm class instance.
  * @retval none
  */
VOID USBD_CDC_ACM_Activate(VOID *cdc_acm_instance)
{
  /* USER CODE BEGIN USBD_CDC_ACM_Activate */
  UX_PARAMETER_NOT_USED(cdc_acm_instance);
  cdc_acm = (UX_SLAVE_CLASS_CDC_ACM*) cdc_acm_instance;
  /* USER CODE END USBD_CDC_ACM_Activate */

  return;
}

/**
  * @brief  USBD_CDC_ACM_Deactivate
  *         This function is called when extraction of a CDC ACM device.
  * @param  cdc_acm_instance: Pointer to the cdc acm class instance.
  * @retval none
  */
VOID USBD_CDC_ACM_Deactivate(VOID *cdc_acm_instance)
{
  /* USER CODE BEGIN USBD_CDC_ACM_Deactivate */
  UX_PARAMETER_NOT_USED(cdc_acm_instance);
  cdc_acm = UX_NULL;
  /* USER CODE END USBD_CDC_ACM_Deactivate */

  return;
}

/**
  * @brief  USBD_CDC_ACM_ParameterChange
  *         This function is invoked to manage the CDC ACM class requests.
  * @param  cdc_acm_instance: Pointer to the cdc acm class instance.
  * @retval none
  */
VOID USBD_CDC_ACM_ParameterChange(VOID *cdc_acm_instance)
{
  /* USER CODE BEGIN USBD_CDC_ACM_ParameterChange */
  UX_PARAMETER_NOT_USED(cdc_acm_instance);
  /* USER CODE END USBD_CDC_ACM_ParameterChange */

  return;
}

/* USER CODE BEGIN 1 */
#define CDC_TX_RING_SIZE  512   /* 2のべき乗 */
#define CDC_TX_CHUNK      63

static uint8_t  tx_ring[CDC_TX_RING_SIZE];
static uint16_t tx_head = 0;          /* 書き込み位置 */
static uint16_t tx_tail = 0;          /* 読み出し位置 */
static uint8_t  tx_chunk[CDC_TX_CHUNK];
static ULONG    tx_chunk_len = 0;     /* >0 なら送信中 */
static ULONG    tx_sent = 0;

/* 送信データをリングに積むだけ（即return）。積めたバイト数を返す */
uint32_t USBD_CDC_ACM_Write(const uint8_t *data, uint32_t len)
{
  uint32_t n = 0;
  if (cdc_acm == UX_NULL) return 0;
  while (n < len) {
    uint16_t next = (tx_head + 1) & (CDC_TX_RING_SIZE - 1);
    if (next == tx_tail) break;       /* full: 超過分は捨てる */
    tx_ring[tx_head] = data[n++];
    tx_head = next;
  }
  return n;
}

/* メインループから毎回呼ぶ。待たずに状態機械を1歩進めるだけ */
void USBD_CDC_ACM_TxTask(void)
{
  UINT ret;

  if (cdc_acm == UX_NULL) {           /* 切断中は全部破棄 */
    tx_head = tx_tail = 0;
    tx_chunk_len = 0;
    return;
  }

  if (tx_chunk_len == 0) {            /* 次のチャンクを作る */
    while (tx_chunk_len < CDC_TX_CHUNK && tx_tail != tx_head) {
      tx_chunk[tx_chunk_len++] = tx_ring[tx_tail];
      tx_tail = (tx_tail + 1) & (CDC_TX_RING_SIZE - 1);
    }
    if (tx_chunk_len == 0) return;    /* 送るものなし */
  }

  /* 完了するまで同じ buffer / length で呼び続ける必要がある */
  ret = ux_device_class_cdc_acm_write_run(cdc_acm, tx_chunk, tx_chunk_len, &tx_sent);
  if (ret == UX_STATE_NEXT || ret == UX_STATE_EXIT) {
    tx_chunk_len = 0;                 /* 完了 or エラー → 次へ */
  }
}

/* 受信完了したときだけ *received にバイト数が入る（未完了は0） */
uint32_t USBD_CDC_ACM_Receive(uint8_t *buffer, uint32_t size, uint32_t *received)
{
  ULONG actual = 0;
  UINT ret;

  *received = 0;
  if (cdc_acm == UX_NULL) return 1;

  ret = ux_device_class_cdc_acm_read_run(cdc_acm, buffer, size, &actual);
  if (ret == UX_STATE_NEXT) {
    *received = actual;
  }
  return 0;
}

/* USER CODE END 1 */
