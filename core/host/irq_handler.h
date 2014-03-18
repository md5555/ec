/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Helper to declare IRQ handling routines */

#ifndef __IRQ_HANDLER_H
#define __IRQ_HANDLER_H

/* Helper macros to build the IRQ handler and priority struct names */
#define IRQ_HANDLER(irqname) CONCAT3(irq_, irqname, _handler)
#define IRQ_PRIORITY(irqname) CONCAT2(prio_, irqname)
/*
 * Macro to connect the interrupt handler "routine" to the irq number "irq" and
 * ensure it is enabled in the interrupt controller with the right priority.
 */
#define DECLARE_IRQ(irq, routine, priority)                     \
	void IRQ_HANDLER(irq)(void)				\
	{							\
		void *ret = __builtin_return_address(0);	\
		task_start_irq_handler(ret);			\
		routine();					\
		task_resched_if_needed(ret);			\
	}							\
	const struct irq_priority IRQ_PRIORITY(irq)		\
	__attribute__((section(".rodata.irqprio")))		\
			= {irq, priority}
#endif  /* __IRQ_HANDLER_H */