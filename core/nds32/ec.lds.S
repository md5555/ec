/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "config.h"

#define FW_OFF_(section) CONFIG_##section##_MEM_OFF
#define FW_OFF(section) (CONFIG_FLASH_BASE + FW_OFF_(section))

#define FW_SIZE_(section) CONFIG_##section##_SIZE
#define FW_SIZE(section) FW_SIZE_(section)


OUTPUT_FORMAT(BFD_FORMAT, BFD_FORMAT, BFD_FORMAT)
OUTPUT_ARCH(BFD_ARCH)
ENTRY(reset)
MEMORY
{
    FLASH (rx) : ORIGIN = FW_OFF(SECTION), LENGTH = FW_SIZE(SECTION)
    IRAM (rw)  : ORIGIN = CONFIG_RAM_BASE, LENGTH = CONFIG_RAM_SIZE
#ifdef CONFIG_LPC
    H2RAM (rw) : ORIGIN = CONFIG_H2RAM_BASE, LENGTH = CONFIG_H2RAM_SIZE
#endif
}
SECTIONS
{
    .text : {
        KEEP(OUTDIR/core/CORE/init.o (.text.vecttable))
        . = ALIGN(4);
        __version_struct_offset = .;
        KEEP(*(.rodata.ver))
#ifdef SHIFT_CODE_FOR_TEST
        . = ALIGN(256);
#else
        . = ALIGN(4);
#endif
        KEEP(OUTDIR/core/CORE/init.o (.text.vectirq))
        KEEP(OUTDIR/core/CORE/init.o (.text))
        *(.text*)

        . = ALIGN(CONFIG_IT83XX_ILM_BLOCK_SIZE);
        __flash_dma_start = .;
        KEEP(*(.flash_direct_map))
        . = ALIGN(CONFIG_IT83XX_ILM_BLOCK_SIZE);
#ifdef COMPILE_FOR_RAM
    } > IRAM
#else
    } > FLASH
#endif
    . = ALIGN(4);
    .rodata :  {
        /* Symbols defined here are declared in link_defs.h */
        __irqprio = .;
        KEEP(*(.rodata.irqprio))
        __irqprio_end = .;

        . = ALIGN(4);
        __irqhandler = .;
        KEEP(OUTDIR/core/CORE/init.o (.rodata.vecthandlers))

        . = ALIGN(4);
        __cmds = .;
        KEEP(*(SORT(.rodata.cmds*)))
        __cmds_end = .;

        . = ALIGN(4);
        __hcmds = .;
        KEEP(*(.rodata.hcmds))
        __hcmds_end = .;

        . = ALIGN(4);
        __hooks_init = .;
        KEEP(*(.rodata.HOOK_INIT))
        __hooks_init_end = .;

        __hooks_pre_freq_change = .;
        KEEP(*(.rodata.HOOK_PRE_FREQ_CHANGE))
        __hooks_pre_freq_change_end = .;

        __hooks_freq_change = .;
        KEEP(*(.rodata.HOOK_FREQ_CHANGE))
        __hooks_freq_change_end = .;

        __hooks_sysjump = .;
        KEEP(*(.rodata.HOOK_SYSJUMP))
        __hooks_sysjump_end = .;

        __hooks_chipset_pre_init = .;
        KEEP(*(.rodata.HOOK_CHIPSET_PRE_INIT))
        __hooks_chipset_pre_init_end = .;

        __hooks_chipset_startup = .;
        KEEP(*(.rodata.HOOK_CHIPSET_STARTUP))
        __hooks_chipset_startup_end = .;

        __hooks_chipset_resume = .;
        KEEP(*(.rodata.HOOK_CHIPSET_RESUME))
        __hooks_chipset_resume_end = .;

        __hooks_chipset_suspend = .;
        KEEP(*(.rodata.HOOK_CHIPSET_SUSPEND))
        __hooks_chipset_suspend_end = .;

        __hooks_chipset_shutdown = .;
        KEEP(*(.rodata.HOOK_CHIPSET_SHUTDOWN))
        __hooks_chipset_shutdown_end = .;

        __hooks_ac_change = .;
        KEEP(*(.rodata.HOOK_AC_CHANGE))
        __hooks_ac_change_end = .;

        __hooks_lid_change = .;
        KEEP(*(.rodata.HOOK_LID_CHANGE))
        __hooks_lid_change_end = .;

        __hooks_pwrbtn_change = .;
        KEEP(*(.rodata.HOOK_POWER_BUTTON_CHANGE))
        __hooks_pwrbtn_change_end = .;

        __hooks_charge_state_change = .;
        KEEP(*(.rodata.HOOK_CHARGE_STATE_CHANGE))
        __hooks_charge_state_change_end = .;

        __hooks_battery_soc_change = .;
        KEEP(*(.rodata.HOOK_BATTERY_SOC_CHANGE))
        __hooks_battery_soc_change_end = .;

        __hooks_tick = .;
        KEEP(*(.rodata.HOOK_TICK))
        __hooks_tick_end = .;

        __hooks_second = .;
        KEEP(*(.rodata.HOOK_SECOND))
        __hooks_second_end = .;

        __deferred_funcs = .;
        KEEP(*(.rodata.deferred))
        __deferred_funcs_end = .;

        . = ALIGN(4);
        *(.rodata*)

#if defined(SECTION_IS_RO) && defined(CONFIG_FLASH)
        . = ALIGN(64);
        KEEP(*(.google))
#endif
        . = ALIGN(4);
#ifdef COMPILE_FOR_RAM
    } > IRAM
#else
    } >FLASH
#endif
    __ro_end = . ;

#ifdef COMPILE_FOR_RAM
    .data : {
#else
    .data : {
#endif
        . = ALIGN(4);
        __data_start = .;
        *(.data.tasks)
        *(.data)
#ifdef CONFIG_MPU
        /* It has to be aligned by 32 bytes to be a valid MPU region. */
        . = ALIGN(32);
        __iram_text_start = .;
#else
        . = ALIGN(4);
#endif
        *(.iram.text)
#ifdef CONFIG_MPU
        . = ALIGN(32);
        __iram_text_end = .;
#else
        . = ALIGN(4);
#endif
        __data_end = .;

    } > IRAM AT>FLASH


    __deferred_funcs_count =
                (__deferred_funcs_end - __deferred_funcs) / 4;
    ASSERT(__deferred_funcs_count <= DEFERRABLE_MAX_COUNT,
           "Increase DEFERRABLE_MAX_COUNT")

    .bss : {
        /* Stacks must be 64-bit aligned */
        . = ALIGN(8);
        __bss_start = .;
        *(.bss.tasks)
        *(.bss.task_scratchpad)
        . = ALIGN(8);
        *(.bss.system_stack)
        /* Rest of .bss takes care of its own alignment */
        *(.bss)
        . = ALIGN(4);
        __bss_end = .;

        /* Shared memory buffer must be at the end of preallocated RAM, so it
         * can expand to use all the remaining RAM. */
        __shared_mem_buf = .;

    } > IRAM

    .flash.tag : {
        /* Tag at end of firmware image so that we can find the image size.
         * This may be overwritten by the shared memory buffer; that's ok
         * because we only use it to find the image size in flash. */
        . = ALIGN(4);
        BYTE(0x45);
        BYTE(0x4e);
        BYTE(0x44);
        BYTE(0xea);
        /* NOTHING MAY GO IN FLASH AFTER THIS! */
    } >FLASH

#ifdef CONFIG_LPC
    .h2ram (NOLOAD) : {
        . += CONFIG_H2RAM_HOST_LPC_IO_BASE;
        *(.h2ram.pool.hostcmd)
        . = ALIGN(256);
        *(.h2ram.pool.acpiec)
    } > H2RAM
#endif

#if !(defined(SECTION_IS_RO) && defined(CONFIG_FLASH))
    /DISCARD/ : {
              *(.google)
              }
#endif

    /DISCARD/ : { *(.ARM.*) }
}
