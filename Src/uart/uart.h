#pragma once
/**
 * @file    uart.h
 * @brief   Sterownik USART dla STM32F411
 *
 *
 * TX — podwójny bufor + DMA:
 *   - Task pisze do BufA przez mutex (bezpieczne wielowątkowo)
 *   - Gdy BufA gotowy — zamiana z BufB, DMA wysyła BufB
 *   - Po TC (Transfer Complete) — sprawdź czy jest coś w nowym BufA
 *   - CPU nie jest przerywany co bajt — jedno przerwanie TC na cały blok
 *
 * RX — DMA circular:
 *   - DMA sam wpisuje odebrane bajty do RxBuf[] bez udziału CPU
 *   - Odczyt przez porównanie pozycji DMA z RxTail
 *   - Zero przerwań przy odbiorze pojedynczych bajtów
 *
 * Pełna konfiguracja przez UART_Config_t —
 * brak hardkodowanych pinów, instancji, prędkości.
 */


#include "stm32f411xe.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <stdint.h>
#include <stdbool.h>

/* ================================================ */
/*              KONFIGURACJA BUFORÓW                */
/* ================================================ */

/** Rozmiar jednego bufora TX (A i B mają taki sam rozmiar) */
#define UART_TX_BUF_SIZE    256

/** Rozmiar bufora RX dla DMA circular */
#define UART_RX_BUF_SIZE    128

/** Maksymalny czas oczekiwania taska na wolny bufor TX [ms] */
#define UART_TX_TIMEOUT_MS  10



/* ================================================ */
/*              STRUKTURY                           */
/* ================================================ */

/**
 * @brief Konfiguracja przekazywana do UART_Init.
 *        Wypełnić przed wywołaniem init.
 */

typedef struct {
    USART_TypeDef           *Instance;      //Instancja
    uint32_t                BaudRate;       //Prędkość
    uint32_t                ClockFreq;      //Częstotliwosć APB w HZ

    GPIO_TypeDef            *TxPort;       
    uint32_t                TxPin;
    uint8_t                 TxAF;
    GPIO_TypeDef            *RxPort;
    uint32_t                RxPin;
    uint8_t                 RxAF;

    DMA_TypeDef             *DmaTx;
    DMA_Stream_TypeDef      *DmaTxStream;    //np. DMA1_Stream6
    uint8_t                 DmaTxChannel;
    IRQn_Type               DmaTxIRQn;

    DMA_TypeDef             *DmaRx;
    DMA_Stream_TypeDef      *DmaRxStream;
    uint8_t                 DmaRxChannel;
    uint8_t                 NvicPriority;   //Priorytet przerwań
} UART_Config_t;

/**
 * @brief Handle — trwały stan sterownika przez cały czas pracy.
 *        Jeden handle na każdy używany UART.
 *
 * ┌─────────────────────────────────────────────────────────────┐
 * │  Przepływ TX:                                               │
 * │  Task → bierze TxMutex → pisze do TxBufA → oddaje mutex    │
 * │       → jeśli DMA wolne: zamień bufory, wystartuj DMA       │
 * │       → jeśli DMA zajęte: czekaj na TxDoneSem              │
 * │  ISR  → TC: daje TxDoneSem, TxDmaBusy = false              │
 * └─────────────────────────────────────────────────────────────┘
 *
 * Nie modyfikować pól bezpośrednio — używaj API.
 */

 typedef struct{
    UART_Config_t           Config;

    uint8_t                 TxBufA[UART_TX_BUF_SIZE];       //Bufor zapisu
    uint8_t                 TxBufB[UART_TX_BUF_SIZE];       //Bufor DMA
    uint8_t                 RxBuf[UART_RX_BUF_SIZE];       //Bufor RX DMA

    volatile uint16_t       TxLenA;                         //ile bajtów w BufA
    volatile bool           TxDmaBusy;                      //czy DMA aktualnie wysyła

    SemaphoreHandle_t       TxMutex;                        //Mutex dla wielowątkowego TX - chroni przed jednoczesnym pisaniem z dwóch taskow
    SemaphoreHandle_t       TxDoneSem;                      // Semafor binarny — ISR daje, task czeka

 }UART_Handle_t;


 void UART_Init(UART_Handle_t *huart, const UART_Config_t *cfg);
 void UART_Transmit(UART_Handle_t *huart, const uint8_t *data, uint16_t len);
 void UART_DMA_TX_IRQHandler(UART_Handle_t *huart);
