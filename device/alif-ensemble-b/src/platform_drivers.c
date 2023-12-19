#include "platform_drivers.h"

#include "RTE_Components.h"
#include CMSIS_device_header
#include "Driver_HWSEM.h"

#include "Driver_GPIO.h"
#include "board.h"
#include "uart_tracelib.h"
#include "log_macros.h"
#include "ethosu_driver.h"
#include "ethosu_npu_init.h"
#include <string.h>
#include "lcd_img.h"

uint32_t tprof1, tprof2, tprof3, tprof4, tprof5;

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
    int ret = 0;
    if (0 != Init_SysTick()) {
        printf("Failed to initialise system tick config\n");
    }

    /* Forces retarget code to be included in build */
    extern void _clock_init(void);
    _clock_init();
#ifdef COPY_VECTORS
    copy_vtor_table_to_ram();
#endif
    extern ARM_DRIVER_HWSEM ARM_Driver_HWSEM_(0);
    ARM_DRIVER_HWSEM *HWSEMdrv = &ARM_Driver_HWSEM_(0);
    HWSEMdrv->Initialize(NULL);
    /* Only 1 core will do the pinmux */
    if (HWSEMdrv->Lock() == ARM_DRIVER_OK) {
        /* We're first to acquire the lock - we do it */
        BOARD_Power_Init();
        BOARD_Clock_Init();
        BOARD_Pinmux_Init();

        /* Lock a second time to raise the count to 2 - the signal that we've finished */
        HWSEMdrv->Lock();
    } else {
        /* Someone else got there first - they did it or are doing it. Wait until we see count 2 indicating they've finished */
        while (HWSEMdrv->GetCount() < 2);
    }

    HWSEMdrv->Uninitialize();
    ret = tracelib_init(NULL);
    if(ret!=0){
        info("uart init failed \n");
        return -1;
    }

// #if defined(ETHOSU_ARCH) && (ETHOSU_ARCH==u55)
#if defined (ARM_NPU)
    if(arm_ethosu_npu_init()!=0){
        info("npu init failed \n");
        return -1;
    }
#else
    info("Ethos Arch not defined");
#endif

    /* Enable the CPU Cache */
    CpuCacheEnable();

    //LCD init
    lcd_init();
    return ret;

}

uint64_t ethosu_address_remap(uint64_t address, int index)
{
    UNUSED(index);
    /* Double cast to avoid build warning about pointer/integer size mismatch */
    return LocalToGlobal((void *) (uint32_t) address);
}
// Stubs to suppress missing stdio definitions for nosys
#define TRAP_RET_ZERO  {__BKPT(0); return 0;}