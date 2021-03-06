/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * N8 CPU initialization
 */

#include "config.h"

/* magic macro to implement IRQ prefix / exit */
.macro vector name
.weak \name\()_handler
.set \name\()_handler, unhandled_irq
j __entry_\()\name
.pushsection .text.vectirq
.global __entry_\()\name
__entry_\()\name:
	/* the context is stored on the current task stack*/
	/* save r15, fp, lp and sp */
	smw.adm $r15, [$sp], $r15, 0xb
	/* r0-r5 are caller saved */
	smw.adm $r0,  [$sp], $r5, 0
	/* switch to system stack if we are called from process stack */
	la $r3, stack_end
	mov55 $fp, $sp
	slt45 $r3, $sp /* if sp > end of system stack, then r15 = 1 and */
	cmovn $sp, $r3, $r15 /* point sp to the top of the system stack */
	/* C routine handler */
	jal \name\()_handler
	/* check whether we need to change the scheduled task */
	lwi.gp  $r2, [ + need_resched]
	bnez $r2, __switch_task
	/* restore r0-r5 */
	lmw.bim $r0, [$fp], $r5, 0
	/* restore r15, fp, lp and sp */
	lmw.bi $r15, [$fp], $r15, 0xb
	/* restore PC and PSW */
	iret
.popsection
.pushsection .rodata.vecthandlers
.long \name\()_handler
.popsection
.endm

.section .text.vecttable

/* Exceptions vector */
vectors:
j reset		/* reset / NMI */
j excep_handler	/* TLB fill */
j excep_handler	/* PTE not present */
j excep_handler	/* TLB misc */
j excep_handler	/* TLB VLPT miss */
j excep_handler	/* Machine error */
j excep_handler	/* Debug related */
j excep_handler	/* General exception */
vector syscall	/* Syscall */
vector irq_0	/* HW  0 */
vector irq_1	/* HW  1 */
vector irq_2	/* HW  2 */
vector irq_3	/* HW  3 */
vector irq_4	/* HW  4 */
vector irq_5	/* HW  5 */
vector irq_6	/* HW  6 */
vector irq_7	/* HW  7 */
vector irq_8	/* HW  8 */
vector irq_9	/* HW  9 */
vector irq_10	/* HW 10 */
vector irq_11	/* HW 11 */
vector irq_12	/* HW 12 */
vector irq_13	/* HW 13 */
vector irq_14	/* HW 14 */
vector irq_15	/* HW 15 */

/* E-flash signature */
.org 0x80
.balign 16
.global eflash_sig
eflash_sig:
.byte 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xB4
.byte 0x85, 0x12, 0x5A, 0x5A, 0xAA, 0xAA, 0x55, 0x55
/* flags: internal oscillator + implicit location */

.text

.global reset
reset:
	/* GP register is used to access .data and .bss */
	la $gp, _SDA_BASE_

	/* Set system stack pointer. */
	la $sp, stack_end

	/* map/enable the 16kB of DLM at 0x00080000 */
	li   $r0, 0x00080005
	mtsr $r0, $mr7

	/* Enable DLM 8k~12K(bit2) and DLM 12k~16k(bit3) */
	la  $r1, 0x00F02030
	lbi $r0, [$r1]
	ori $r0, $r0, 0x0C
	sbi $r0, [$r1]

	/* Clear BSS */
	la   $r0, _bss_start
	lwi  $r1, [$r0]
	la   $r0, _bss_end
	lwi  $r2, [$r0]
	movi $r0, #0
bss_loop:
	swi.bi $r0, [$r1], 4
	bne $r1, $r2, bss_loop

	/* Copy initialized data to DLM */
	la $r0, _data_start
	lwi  $r1, [$r0]
	la $r0, _data_end
	lwi  $r2, [$r0]
	la $r0, _ro_end
	lwi  $r0, [$r0]
data_loop:
	lwi.bi $r3, [$r0], 4
	swi.bi $r3, [$r1], 4
	bne $r1, $r2, data_loop

	/* we switch to our own exception vectors */
	/* go back to it level 0 with HW interrupts globally disabled */
	li   $r4, 0x70008
	mtsr $r4, $PSW
	/* IT8380 specific: set vectors at 0 */
	li   $r5, 0x0F02041 /* IVTBAR in GCTRL */
	movi $r15, 0
	sbi  $r15, [$r5]
	/* Interrupt vectors are every 4 bytes */
	li   $r5, 0x00000007
	mtsr $r5, $IVB

	/* Jump to C routine */
	jal main

	/* That should not return.  If it does, loop forever. */
	j .

.global unhandled_irq
unhandled_irq:
	mfsr $gp, $ITYPE
	sethi $r15, 0xBAD0
	or   $r15, $r15, $gp
	mtsr $r15, $ITYPE
	dsb
	j excep_handler /* display exception with ITYPE=bad00<irq> */

.global excep_handler
excep_handler:
	/* safety: reload GP even though it should be already set */
	la $gp, _SDA_BASE_
	/* save r0 to free one register */
	swi.gp  $r0, [ + saved_regs]
	/* save the remaining 15 registers */
	la      $r0, saved_regs + 4
	smw.bim $r1, [$r0], $r10, 0
	smw.bim $r15,[$r0], $r15, 0xF
	/* put a sane stack pointer */
	la $sp, stack_end
	/* add IPC, IPSW to the context */
	mfsr    $r1, $IPC
	mfsr    $r2, $IPSW
	smw.bi  $r1, [$r0], $r2, 0
	/* pass ir6/ITYPE as the second parameter */
	mfsr    $r1, $ITYPE
	/* exception context pointer as first parameter */
	addi    $r0, $r0, -16*4
	/* jump to panic dump C routine */
	jal report_panic
	/* we never return: exceptions are fatal */
	j .

_bss_start:
.long __bss_start
_bss_end:
.long __bss_end
_data_start:
.long __data_start
_data_end:
.long __data_end
_ro_end:
.long __ro_end

/* Reserve space for system stack */
.section .bss.system_stack
stack_start:
.space CONFIG_STACK_SIZE, 0
stack_end:
.global stack_end
/* registers state at exception entry */
.global saved_regs
saved_regs:
.long 0, 0, 0, 0, 0, 0, 0, 0
.long 0, 0, 0, 0, 0, 0, 0, 0
/* IPC, IPSW for convenient access */
.long 0, 0
