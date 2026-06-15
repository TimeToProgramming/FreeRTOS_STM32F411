#include "clock.h"
#include "stm32f411xe.h"

/*
 *
 * Cel: SYSCLK = 100 MHz z zewnętrznego kwarcu HSE 8 MHz przez PLL
 *
 * Sekwencja inicjalizacji:
 *
 * 1. Włączenie HSE (zewnętrzny kwarc 8 MHz) i oczekiwanie na stabilizację (HSERDY)
 *
 * 2. Flash latency = 3 Wait States — wymagane przy 100 MHz (RM0383 tabela 10)
 *    + włączenie Prefetch, Instruction Cache i Data Cache
 *
 * 3. Prescalery magistral:
 *    AHB  = /1 → 100 MHz  (magistrala rdzenia, DMA, GPIO)
 *    APB1 = /2 → 50 MHz   (I2C1, SPI2, USART2) — max 50 MHz dla F411
 *    APB2 = /1 → 100 MHz  (SPI1, USART1, TIM1)
 *
 * 4. Konfiguracja PLL (źródło = HSE):
 *    PLLM = 8   → VCO input  = 8 MHz / 8 = 1 MHz
 *    PLLN = 200 → VCO output = 1 MHz * 200 = 200 MHz
 *    PLLP = /2  → SYSCLK     = 200 MHz / 2 = 100 MHz
 *    PLLQ = 4   → USB clock  = 200 MHz / 4 = 50 MHz (nieużywane, wymagane przez HW)
 *
 * 5. Włączenie PLL i oczekiwanie na stabilizację (PLLRDY)
 *
 * 6. Przełączenie SYSCLK na PLL i oczekiwanie na potwierdzenie (SWS == PLL)
 *
 * 7. Aktualizacja zmiennej SystemCoreClock = 100 000 000 Hz
 *    (używana przez FreeRTOS do obliczenia ticków SysTick)
 */

void SystemClock_Config(void){
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY));

    /* FLASH WAIT STATE */
    FLASH->ACR = FLASH_ACR_LATENCY_3WS
               | FLASH_ACR_PRFTEN
               | FLASH_ACR_ICEN
               | FLASH_ACR_DCEN;

    /* RCC CFGR AHB/APB prescaller */
    RCC->CFGR = RCC_CFGR_HPRE_DIV1
              | RCC_CFGR_PPRE1_DIV2
              | RCC_CFGR_PPRE2_DIV1;

    /* PLL Configuration */
    RCC->PLLCFGR = (4U << RCC_PLLCFGR_PLLQ_Pos)
                    |(200U << RCC_PLLCFGR_PLLN_Pos)
                    |(8U << RCC_PLLCFGR_PLLM_Pos)
                    |(0U << RCC_PLLCFGR_PLLP_Pos)
                    |RCC_PLLCFGR_PLLSRC_HSE;

    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY));

    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);

    SystemCoreClock = 100000000UL;

}