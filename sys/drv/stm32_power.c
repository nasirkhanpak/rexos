/*
    RExOS - embedded RTOS
    Copyright (c) 2011-2014, Alexey Kramarenko
    All rights reserved.
*/

#include "stm32_power.h"
#include "../../userspace/core/core.h"
#include "../../userspace/ipc.h"
#include "../../userspace/lib/stdio.h"
#include "../../sys/sys_call.h"
#include "sys_config.h"

//remove me later
#include "../../kernel/kernel.h"

#if defined(STM32F1)
#define MAX_APB2                             72000000
#define MAX_APB1                             36000000
#elif defined(STM32F2)
#define MAX_APB2                             60000000
#define MAX_APB1                             30000000
#elif defined(STM32F401) || defined(STM32F411)
#define MAX_APB2                             100000000
#define MAX_APB1                             50000000
#elif defined(STM32F405) || defined(STM32F407) || defined(STM32F415) || defined(STM32F417)
#define MAX_APB2                             84000000
#define MAX_APB1                             42000000
#elif defined(STM32F427) || defined(STM32F429) || defined(STM32F437) || defined(STM32F439)
#define MAX_APB2                             90000000
#define MAX_APB1                             45000000
#endif

#if defined(STM32F1)
#define PPRE1                                8
#define PPRE2                                11
#elif defined(STM32F2) || defined(STM32F4)
#define PPRE1                                10
#define PPRE2                                13
#endif

#ifndef RCC_CSR_WDGRSTF
#define RCC_CSR_WDGRSTF RCC_CSR_IWDGRSTF
#endif

#ifndef RCC_CSR_PADRSTF
#define RCC_CSR_PADRSTF RCC_CSR_PINRSTF
#endif

void stm32_power();

const REX __STM32_POWER = {
    //name
    "STM32 power",
    //size
    512,
    //priority - driver priority
    90,
    //flags
    PROCESS_FLAGS_ACTIVE,
    //ipc size
    10,
    //function
    stm32_power
};


#if defined(STM32F100)
static inline void setup_pll(int mul, int div)
{
    RCC->CFGR &= ~((0xf << 18) | (1 << 16));
    RCC->CFGR2 &= ~0xf;
    RCC->CFGR2 |= (div - 1) << 0;
    RCC->CFGR |= ((mul - 2) << 18);
#if (HSE_VALUE)
    if (RCC->CR & RCC_CR_HSEON)
        RCC->CFGR |= (1 << 16);
#endif
}

static inline int get_pll_clock()
{
    int pllsrc = HSI_VALUE / 2;
#if (HSE_VALUE)
    if (RCC->CFGR & (1 << 16))
        pllsrc = HSE_VALUE / ((RCC->CFGR2 & 0xf) + 1);
#endif
    return pllsrc * (((RCC->CFGR >> 18) & 0xf) + 2);
}

#elif defined(STM32F10X_CL)
static inline void setup_pll(int mul, int div, int pll2_mul, int pll2_div)
{
    RCC->CR &= ~RCC_CR_PLL2ON;
    RCC->CFGR &= ~((0xf << 18) | (1 << 16));
    RCC->CFGR2 &= ~(0xf | (1 << 16));
    if (pll2_mul && pll2_div)
    {
        RCC->CFGR2 |= ((pll2_div - 1) << 4) | ((pll2_mul - 2) << 8);
        //turn PLL2 ON
        RCC->CR |= RCC_CR_PLL2ON;
        while (!(RCC->CR & RCC_CR_PLL2RDY)) {}
        RCC->CFGR2 |= (1 << 16);
    }
    RCC->CFGR2 |= (div - 1) << 0;
    RCC->CFGR |= ((mul - 2) << 18);
#if (HSE_VALUE)
    if (RCC->CR & RCC_CR_HSEON)
        RCC->CFGR |= (1 << 16);
#endif
}

static inline int get_pll_clock()
{
    int pllsrc = HSI_VALUE / 2;
    int pllmul;
#if (HSE_VALUE)
    int preddivsrc = HSE_VALUE;
    int pllmul2;
    if (RCC->CFGR & (1 << 16))
    {
        if (RCC->CFGR2 & (1 << 16))
        {
            pllmul2 = ((RCC->CFGR2 >> 8) & 0xf) + 2;
            if (pllmul2 == PLL2_MUL_20)
                pllmul2 = 20;
            preddivsrc = HSE_VALUE / (((RCC->CFGR2 >> 4) & 0xf) + 1) * pllmul2;
        }
        pllsrc = preddivsrc / ((RCC->CFGR2 & 0xf) + 1);
    }
#endif
    pllmul = ((RCC->CFGR >> 18) & 0xf) + 2;
    if (pllmul == PLL_MUL_6_5)
        return pllsrc / 10 * 65;
    else
        return pllsrc * pllmul;
}

#elif defined (STM32F1)
static inline void setup_pll(int mul, int div)
{
    RCC->CFGR &= ~((0xf << 18) | (1 << 16) | (1 << 17));
    RCC->CFGR |= ((mul - 2) << 18);
#if (HSE_VALUE)
    if (RCC->CR & RCC_CR_HSEON)
        RCC->CFGR |= (1 << 16) | ((div - 1)  << 17);
#endif
}

static inline int get_pll_clock()
{
    int pllsrc = HSI_VALUE / 2;
#if (HSE_VALUE)
    if (RCC->CFGR & (1 << 16))
        pllsrc = HSE_VALUE / (((RCC->CFGR >> 17) & 0x1) + 1);
#endif
    return pllsrc * (((RCC->CFGR >> 18) & 0xf) + 2);
}

#elif defined (STM32F2) || defined (STM32F4)
static inline void setup_pll(int m, int n, int p)
{
    RCC->PLLCFGR &= ~((0x3f << 0) | (0x1ff << 6) | (3 << 16) | (1 << 22));
    RCC->PLLCFGR = (m << 0) | (n << 6) | (((p >> 1) -1) << 16);
#if (HSE_VALUE)
    if (RCC->CR & RCC_CR_HSEON)
        RCC->PLLCFGR = (1 << 22);
#endif
}

static inline int get_pll_clock()
{
    int pllsrc = HSI_VALUE;
#if (HSE_VALUE)
    if (RCC->CFGR & (1 << 22))
        pllsrc = HSE_VALUE;
#endif
    return pllsrc / (RCC->PLLCFGR & 0x3f) * ((RCC->PLLCFGR  >> 6) & 0x1ff) / ((((RCC->PLLCFGR  >> 16) & 0x3) + 1) << 1);
}
#endif

int get_core_clock()
{
    switch (RCC->CFGR & (3 << 2))
    {
    case RCC_CFGR_SWS_HSI:
        return HSI_VALUE;
        break;
    case RCC_CFGR_SWS_HSE:
#if (HSE_VALUE)
        return HSE_VALUE;
#endif
        break;
    case RCC_CFGR_SWS_PLL:
        return get_pll_clock();
    }
    return 0;
}

int get_ahb_clock()
{
    int div = 1;
    if (RCC->CFGR & (1 << 7))
        div = 1 << (((RCC->CFGR >> 4) & 7) + 1);
    if (div >= 32)
        div <<= 1;
    return get_core_clock() / div;
}

int get_apb2_clock()
{
    int div = 1;
    if (RCC->CFGR & (1 << (PPRE2 + 2)))
        div = 1 << (((RCC->CFGR >> PPRE2) & 3) + 1);
    return get_ahb_clock() /div;
}

int get_apb1_clock()
{
    int div = 1;
    if (RCC->CFGR & (1 << (PPRE1 + 2)))
        div = 1 << (((RCC->CFGR >> PPRE1) & 3) + 1);
    return get_ahb_clock() / div;
}

RESET_REASON get_reset_reason()
{
    RESET_REASON res = RESET_REASON_UNKNOWN;
    if (RCC->CSR & RCC_CSR_LPWRRSTF)
        res = RESET_REASON_LOW_POWER;
    else if (RCC->CSR & (RCC_CSR_WWDGRSTF | RCC_CSR_WDGRSTF))
        res = RESET_REASON_WATCHDOG;
    else if (RCC->CSR & RCC_CSR_SFTRSTF)
        res = RESET_REASON_SOFTWARE;
    else if (RCC->CSR & RCC_CSR_PORRSTF)
        res = RESET_REASON_POWERON;
    else if (RCC->CSR & RCC_CSR_PADRSTF)
        res = RESET_REASON_PIN_RST;
    return res;
}

void setup_clock(int param1, int param2, int param3)
{
    int i, bus, pll_clock;
    //1. switch to HSI (if not already)
    if (((RCC->CFGR >> 2) & 3) != RCC_CFGR_SW_HSI)
    {
        RCC->CFGR &= ~3;
        while ((RCC->CFGR & (3 << 2)) != RCC_CFGR_SWS_HSI) {}
    }
    //2. try to turn HSE on (if not already, and if HSE_VALUE is set)
#if (HSE_VALUE)
    if ((RCC->CR & RCC_CR_HSEON) == 0)
    {
        RCC->CR |= RCC_CR_HSEON;
        for (i = 0; i < HSE_STARTUP_TIMEOUT; ++i)
            if (RCC->CR & RCC_CR_HSERDY)
                break;
    }
#endif
    //3. setup pll
    RCC->CR &= ~RCC_CR_PLLON;
#if defined(STM32F10X_CL)
    setup_pll(param1, param2, param3 & 0xffff, (param3 >> 16));
#elif defined(STM32F1)
    setup_pll(param1, param2);
#elif defined(STM32F2) || defined(STM32F4)
    setup_pll(param1, param2, param3);
#endif
    //turn PLL on
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {}
    pll_clock = get_pll_clock();

    //4. setup bases
    RCC->CFGR &= ~((0xf << 4) | (0x7 << PPRE1) | (0x7 << PPRE2));
    //AHB. Can operates at maximum clock
    //APB2.
    for (i = 1, bus = 0; (i <= 16) && (pll_clock / i > MAX_APB2); i *= 2, ++bus) {}
    if (bus)
        RCC->CFGR |= (4 | (bus - 1)) << PPRE2;
    //remove this shit later
    __KERNEL->apb2_freq = pll_clock / i;
    //APB1.
    for (; (i <= 16) && (pll_clock / i > MAX_APB1); i *= 2, ++bus) {}
    if (bus)
        RCC->CFGR |= (4 | (bus - 1)) << PPRE1;
    //remove this shit later
    __KERNEL->apb1_freq = pll_clock / i;

    //5. tune flash latency
#if defined(STM32F1) && !defined(STM32F100)
    FLASH->ACR = FLASH_ACR_PRFTBE | (pll_clock / 24000000);
#elif defined(STM32F2) || defined(STM32F4)
    FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN | (pll_clock / 30000000);
#endif
    //6. switch to PLL
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & (3 << 2)) != RCC_CFGR_SWS_PLL) {}

    //remove this shit later
    __KERNEL->ahb_freq = pll_clock;
}

static void inline update_clock(int param1, int param2, int param3)
{
    unsigned int apb1 = RCC->APB1ENR;
    RCC->APB1ENR = 0;
    unsigned int apb2 = RCC->APB2ENR;
    RCC->APB2ENR = 0;
#if defined(STM32F1)
    unsigned int ahb = RCC->AHBENR;
    RCC->AHBENR = 0;
#elif defined (STM32F2) || defined(STM32F4)
    unsigned int ahb1 = RCC->AHB1ENR;
    RCC->AHB1ENR = 0;
    unsigned int ahb2 = RCC->AHB2ENR;
    RCC->AHB2ENR = 0;
    unsigned int ahb3 = RCC->AHB3ENR;
    RCC->AHB3ENR = 0;
#endif

    RCC->CFGR = 0;
#if defined(STM32F10X_CL) || defined(STM32F100)
    RCC->CFGR2 = 0;
#endif

#if defined(STM32F10X_CL)
    setup_clock(PLL_MUL, PLL_DIV, PLL2_MUL | (PLL2_DIV << 16));
#elif defined(STM32F1)
    setup_clock(PLL_MUL, PLL_DIV);
#elif defined(STM32F2) || defined(STM32F4)
    setup_clock(PLL_M, PLL_N, PLL_P);
#endif

#if defined(STM32F1)
    RCC->AHBENR = ahb;
#elif defined (STM32F2) || defined(STM32F4)
    RCC->AHB1ENR = ahb1;
    RCC->AHB2ENR = ahb2;
    RCC->AHB3ENR = ahb3;
#endif
    RCC->APB1ENR = apb1;
    RCC->APB2ENR = apb2;
}

#if (SYS_DEBUG)
static inline void stm32_power_info()
{
    printf("STM32 power driver info\n\r\n\r");
    printf("Core clock: %d\n\r", get_core_clock());
    printf("AHB clock: %d\n\r", get_ahb_clock());
    printf("APB1 clock: %d\n\r", get_apb1_clock());
    printf("APB2 clock: %d\n\r", get_apb2_clock());

    printf("Reset reason: ");
    switch (get_reset_reason())
    {
    case RESET_REASON_LOW_POWER:
        printf("low power");
        break;
    case RESET_REASON_WATCHDOG:
        printf("watchdog");
        break;
    case RESET_REASON_SOFTWARE:
        printf("software");
        break;
    case RESET_REASON_POWERON:
        printf("power ON");
        break;
    case RESET_REASON_PIN_RST:
        printf("pin reset");
        break;
    default:
        printf("unknown");
    }
    printf("\n\r\n\r");
}
#endif

static inline void stm32_power_loop()
{
    IPC ipc;
    for (;;)
    {
        ipc_wait_peek_ms(&ipc, 0, 0);
        switch (ipc.cmd)
        {
        case IPC_PING:
            ipc.cmd = IPC_PONG;
            ipc_post(&ipc);
            break;
        case SYS_SET_STDOUT:
            __HEAP->stdout = (STDOUT)ipc.param1;
            __HEAP->stdout_param = (void*)ipc.param2;
            break;
#if (SYS_DEBUG)
        case SYS_GET_INFO:
            stm32_power_info();
            break;
#endif
        case IPC_GET_CLOCK:
            switch (ipc.param1)
            {
            case STM32_CLOCK_CORE:
                ipc.param1 = get_core_clock();
                break;
            case STM32_CLOCK_AHB:
                ipc.param1 = get_ahb_clock();
                break;
            case STM32_CLOCK_APB1:
                ipc.param1 = get_apb1_clock();
                break;
            case STM32_CLOCK_APB2:
                ipc.param1 = get_apb2_clock();
                break;
            default:
                ipc.cmd = IPC_INVALID_PARAM;
            }
            ipc_post(&ipc);
            break;
        case IPC_UPDATE_CLOCK:
            update_clock(ipc.param1, ipc.param2, ipc.param3);
            break;
        case IPC_GET_RESET_REASON:
            ipc.param1 = get_reset_reason();
            ipc_post(&ipc);
            break;
        }
    }
}

void stm32_power()
{
    RCC->APB1ENR = 0;
    RCC->APB2ENR = 0;

#if defined(STM32F1)
    RCC->AHBENR = 0;
#elif defined(STM32F2) || defined(STM32F4)
    RCC->AHB1ENR = 0;
    RCC->AHB2ENR = 0;
    RCC->AHB3ENR = 0;
#endif

    RCC->CFGR = 0;
#if defined(STM32F10X_CL) || defined(STM32F100)
    RCC->CFGR2 = 0;
#endif

#if defined(STM32F10X_CL)
    setup_clock(PLL_MUL, PLL_DIV, PLL2_MUL | (PLL2_DIV << 16));
#elif defined(STM32F1)
    setup_clock(PLL_MUL, PLL_DIV);
#elif defined(STM32F2) || defined(STM32F4)
    setup_clock(PLL_M, PLL_N, PLL_P);
#endif

    sys_post(SYS_SET_POWER, 0, 0, 0);
    stm32_power_loop();
}
