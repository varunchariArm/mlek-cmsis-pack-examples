#include "platform_drivers.h"

#include "RTE_Components.h"
#include CMSIS_device_header

#include "Driver_GPIO.h"
#include "board.h"
#include "uart_tracelib.h"
#include "log_macros.h"
#include "ethosu_driver.h"
#include "ethosu_npu_init.h"
#include <string.h>

#define _GET_DRIVER_REF(ref, peri, chan) \
    extern ARM_DRIVER_##peri Driver_##peri##chan; \
    static ARM_DRIVER_##peri * ref = &Driver_##peri##chan;
#define GET_DRIVER_REF(ref, peri, chan) _GET_DRIVER_REF(ref, peri, chan)

GET_DRIVER_REF(gpio_b, GPIO, BOARD_LEDRGB0_B_GPIO_PORT);
GET_DRIVER_REF(gpio_r, GPIO, BOARD_LEDRGB0_R_GPIO_PORT);

#ifdef COPY_VECTORS

static VECTOR_TABLE_Type MyVectorTable[496] __attribute__((aligned (2048))) __attribute__((section (".bss.noinit.ram_vectors")));

static void copy_vtor_table_to_ram()
{
    if (SCB->VTOR == (uint32_t) MyVectorTable) {
        return;
    }
    memcpy(MyVectorTable, (const void *) SCB->VTOR, sizeof MyVectorTable);
    __DMB();
    // Set the new vector table into use.
    SCB->VTOR = (uint32_t) MyVectorTable;
    __DSB();
}
#endif

/**
 * @brief  CPU L1-Cache enable.
 * @param  None
 * @retval None
 */
static void CpuCacheEnable(void)
{
    /* Enable I-Cache */
    SCB_EnableICache();

    /* Enable D-Cache */
    SCB_EnableDCache();
}

int platform_init (void)
{
    gpio_b->Initialize(BOARD_LEDRGB0_B_PIN_NO, NULL);
    gpio_b->PowerControl(BOARD_LEDRGB0_B_PIN_NO, ARM_POWER_FULL);
    gpio_b->SetDirection(BOARD_LEDRGB0_B_PIN_NO, GPIO_PIN_DIRECTION_OUTPUT);
    gpio_b->SetValue(BOARD_LEDRGB0_B_PIN_NO, GPIO_PIN_OUTPUT_STATE_LOW);

    gpio_r->Initialize(BOARD_LEDRGB0_R_PIN_NO, NULL);
    gpio_r->PowerControl(BOARD_LEDRGB0_R_PIN_NO, ARM_POWER_FULL);
    gpio_r->SetDirection(BOARD_LEDRGB0_R_PIN_NO, GPIO_PIN_DIRECTION_OUTPUT);
    gpio_r->SetValue(BOARD_LEDRGB0_R_PIN_NO, GPIO_PIN_OUTPUT_STATE_LOW);
    
    int ret = 0;
#ifdef COPY_VECTORS
    copy_vtor_table_to_ram();
#endif
    BOARD_Power_Init();
    BOARD_Clock_Init();
    BOARD_Pinmux_Init();
    gpio_r->SetValue(BOARD_LEDRGB0_R_PIN_NO, GPIO_PIN_OUTPUT_STATE_TOGGLE);
    ret = tracelib_init(NULL);
    if(ret!=0){
        info("uart init failed \n");
        return -1;
    }

#if defined(ETHOSU_ARCH) && (ETHOSU_ARCH==u55)
    if(arm_ethosu_npu_init()!=0){
        info("npu init failed \n");
        return -1;
    }
#else
    info("Ethos Arch not defined");
#endif

#ifdef CORE_M55_HE
    SysTick_Config(SystemCoreClock/10);
#else
    SysTick_Config(SystemCoreClock/25);
#endif
    /* Enable the CPU Cache */
    CpuCacheEnable();
    return ret;

}

void SysTick_Handler (void)
{
#ifdef CORE_M55_HE
    gpio_b->SetValue(BOARD_LEDRGB0_B_PIN_NO, GPIO_PIN_OUTPUT_STATE_TOGGLE);
#else
    gpio_r->SetValue(BOARD_LEDRGB0_R_PIN_NO, GPIO_PIN_OUTPUT_STATE_TOGGLE);
#endif
}

void Toggle_Red_Led(void){
    gpio_r->SetValue(BOARD_LEDRGB0_R_PIN_NO, GPIO_PIN_OUTPUT_STATE_TOGGLE);
}

void Toggle_Green_Led(void){
    gpio_r->SetValue(BOARD_LEDRGB1_G_PIN_NO, GPIO_PIN_OUTPUT_STATE_TOGGLE);
}

uint64_t ethosu_address_remap(uint64_t address, int index)
{
    UNUSED(index);
    /* Double cast to avoid build warning about pointer/integer size mismatch */
    return LocalToGlobal((void *) (uint32_t) address);
}
typedef struct address_range
{
    uintptr_t base;
    uintptr_t limit;
} address_range_t;

const address_range_t no_need_to_invalidate_areas[] = {
    /* TCM is never cached */
    {
        .base = 0x00000000,
        .limit = 0x01FFFFFF,
    },
    {
        .base = 0x20000000,
        .limit = 0x21FFFFFF,
    },
    /* MRAM should never change while running */
    {
        .base = MRAM_BASE,
        .limit = MRAM_BASE + MRAM_SIZE - 1,
    }
};

static bool check_need_to_invalidate(const void *p, size_t bytes)
{
    uintptr_t base = (uintptr_t) p;
    if (bytes == 0) {
        return false;
    }
    uintptr_t limit = base + bytes - 1;
    for (unsigned int i = 0; i < sizeof no_need_to_invalidate_areas / sizeof no_need_to_invalidate_areas[0]; i++) {
        if (base >= no_need_to_invalidate_areas[i].base && limit <= no_need_to_invalidate_areas[i].limit) {
            return false;
        }
    }
    return true;
}

bool ethosu_area_needs_invalidate_dcache(const void *p, size_t bytes)
{
    /* API says null pointer can be passed */
    if (!p) {
        return true;
    }
    /* We know we have a cache and assume the cache is on */
    return check_need_to_invalidate(p, bytes);
}

bool ethosu_area_needs_flush_dcache(const void *p, size_t bytes)
{
    /* We're not using writeback for any Ethos areas */
    UNUSED(p);
    UNUSED(bytes);
    return false;
}

// Stubs to suppress missing stdio definitions for nosys
#define TRAP_RET_ZERO  {__BKPT(0); return 0;}
// int _close(int val) TRAP_RET_ZERO
// int _lseek(int val0, int val1, int val2) TRAP_RET_ZERO
// int _read(int val0, char * val1, int val2) TRAP_RET_ZERO
// int _write(int val0, char * val1, int val2) TRAP_RET_ZERO