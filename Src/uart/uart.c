#include "uart.h"
#include "stm32f411xe.h"
#include <stdint.h>
#include <string.h>
#include "gpio.h"


/* ================================================ */
/*                  INICJALIZACJA                   */
/* ================================================ */

void UART_Init(UART_Handle_t *huart, const UART_Config_t *cfg){

    huart->Config = *cfg;
    huart->TxLenA    = 0;
    huart->TxDmaBusy = false;
    huart->RxTail    = 0;
    memset(huart->TxBufA, 0, sizeof(huart->TxBufA));
    memset(huart->TxBufB, 0, sizeof(huart->TxBufB));

    huart->TxMutex = xSemaphoreCreateMutex();
    configASSERT(huart->TxMutex != NULL);

    /* ZEGARY */
    GPIO_EnableClock(cfg->TxPort);
    GPIO_EnableClock(cfg->RxPort);

    if (cfg->Instance == USART1) { RCC->APB2ENR |= RCC_APB2ENR_USART1EN; }
    else if (cfg->Instance == USART2) { RCC->APB1ENR |= RCC_APB1ENR_USART2EN; }
    else if (cfg->Instance == USART6) { RCC->APB2ENR |= RCC_APB2ENR_USART6EN; }

    if(cfg->DmaTx == DMA1) { RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN; }
    if(cfg->DmaTx == DMA2) { RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN; }

    /* PORTY */
    GPIO_ConfigPin(cfg->TxPort, cfg->TxPin, GPIO_MODE_AF, GPIO_PULL_NONE, GPIO_OTYPE_PP);
    GPIO_ConfigPin(cfg->RxPort, cfg->RxPin, GPIO_MODE_AF, GPIO_PULL_UP, GPIO_OTYPE_PP);

    GPIO_SetAF(cfg->TxPort, cfg->TxPin, cfg->TxAF);
    GPIO_SetAF(cfg->RxPort, cfg->RxPin, cfg->RxAF);

    /* UART */
    if (cfg->Instance == USART1) {
        RCC->APB2RSTR |=  RCC_APB2RSTR_USART1RST;
        RCC->APB2RSTR &= ~RCC_APB2RSTR_USART1RST;
    } else if (cfg->Instance == USART2) {
        RCC->APB1RSTR |=  RCC_APB1RSTR_USART2RST;
        RCC->APB1RSTR &= ~RCC_APB1RSTR_USART2RST;
    } else if (cfg->Instance == USART6) {
        RCC->APB2RSTR |=  RCC_APB2RSTR_USART6RST;
        RCC->APB2RSTR &= ~RCC_APB2RSTR_USART6RST;
    }

    cfg->Instance->BRR = (cfg->ClockFreq + cfg->BaudRate / 2U) / cfg->BaudRate;
    /*
     *    TE  — włącz nadajnik
     *    RE  — włącz odbiornik
     *    UE  — włącz USART (musi być ostatni)
    */

    cfg->Instance->CR1 |= USART_CR1_TE
                        |USART_CR1_RE
                        |USART_CR1_UE;
    /*
     *    DMAR — USART przekazuje odebrane bajty do DMA zamiast
     *    generować przerwanie RXNE przy każdym bajcie.
    */          
    cfg->Instance->CR3 |= USART_CR3_DMAT;

    /*
     *    DMA RX — tryb circular:
     *    Stream odbiera bajty z DR UARTa i wpisuje do RxBuf[].
     *    CIRC — gdy dojdzie do końca bufora, wraca na początek.
     *    NDTR jest automatycznie przeładowywany przez hardware.
     */

    DMA_Stream_TypeDef *dma_rx = cfg->DmaRxStream;
    dma_rx->CR = 0;
    while (dma_rx->CR & DMA_SxCR_EN);

    dma_rx->PAR = (uint32_t)&cfg->Instance->DR;
    dma_rx->M0AR = (uint32_t)huart->RxBuf;
    dma_rx->NDTR = UART_RX_BUF_SIZE;
    dma_rx->CR = ((uint32_t)cfg->DmaRxChannel << DMA_SxCR_CHSEL_Pos)
                |DMA_SxCR_MINC
                |DMA_SxCR_CIRC;
    dma_rx->CR |= DMA_SxCR_EN;

    NVIC_SetPriority(cfg->DmaTxIRQn, cfg->NvicPriority);
    NVIC_EnableIRQ(cfg->DmaTxIRQn);

}


/* ================================================ */
/*                  WYSYŁANIE TX                    */
/* ================================================ */

static void dma_tx_start(UART_Handle_t *huart) {
    /*
    * -kopiujemy bufor A do bufora B,
    * -czyścimy rozmiar bufora A po skopiowaniu (zwalniamy)
    * -Ustawiamy DMA busy
    * -wyłączamy stream przed konfiguracją DMA
    * -czyścimy flagi stanów
    * -ustawiamy źródło i cel DMA
    * -konfugracja streamu
    * -włączenie DMA TX w UART
    * -Start streamu DMA      
    */

     DMA_Stream_TypeDef *dma = huart->Config.DmaTxStream; //wskaźnik na dokładny stram

     /* KOPIOWANIE BUFORA B DO BUFORA A */
     memcpy(huart->TxBufB, huart->TxBufA, huart->TxLenA);
     uint16_t len = huart->TxLenA;

    /* Wyczyszczenie BufA i jego licznik — task może pisać od nowa */
    huart->TxLenA = 0;

    /* Oznaczenie DMA jako zajęte — flaga sprawdzana w UART_Printf */
    huart->TxDmaBusy = true;

    /* Wyłączenie stream przed rekonfiguracją — wymóg hardware */
    dma->CR &= ~(DMA_SxCR_EN);
    while(dma->CR & DMA_SxCR_EN);

  /*
     * Wyczyszczenie flagi statusu DMA w rejestrze IFCR.
     * stare flagi mogą natychmiast wyzwolić przerwanie po starcie, 
     * zanim cokolwiek zostanie wysłane.
     * Stream6 → bity w HIFCR (strumienie 4-7)
     */
    DMA1->HIFCR = DMA_HIFCR_CTCIF6
                | DMA_HIFCR_CHTIF6
                | DMA_HIFCR_CTEIF6
                | DMA_HIFCR_CDMEIF6
                | DMA_HIFCR_CFEIF6;

    /* Adres źródła (pamięć) i cel (rejestr DR UARTa) */
    dma->M0AR = (uint32_t)huart->TxBufB;                //skąd brać dane (Memory 0 Address Registe)
    dma->PAR  = (uint32_t)&huart->Config.Instance->DR;  //gdzie wklejać dane z M0AR (Peripheral Address Register)
    dma->NDTR = len;

        /*
     * Konfiguracja streamu:
     * - CHSEL: numer kanału DMA (przypisanie sprzętowe)
     * - MINC:  inkrementacja adresu pamięci (kolejne bajty bufora)
     * - DIR:   kierunek pamięć → peryferium
     * - TCIE:  przerwanie po Transfer Complete
     */
    dma->CR = ((uint32_t)huart->Config.DmaTxChannel << DMA_SxCR_CHSEL_Pos)
            | DMA_SxCR_MINC
            | (1U << DMA_SxCR_DIR_Pos)
            | DMA_SxCR_TCIE;

    /* Włączenie DMA TX w USART */
    huart->Config.Instance->CR3 |= USART_CR3_DMAT;

    /* Start — od tej chwili DMA wysyła bez udziału CPU */
    dma->CR |= DMA_SxCR_EN;





}