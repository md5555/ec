/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "acpi.h"
#include "common.h"
#include "console.h"
#include "dptf.h"
#include "hooks.h"
#include "host_command.h"
#include "lpc.h"
#include "ec_commands.h"
#include "pwm.h"
#include "timer.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_LPC, outstr)
#define CPRINTF(format, args...) cprintf(CC_LPC, format, ## args)
#define CPRINTS(format, args...) cprints(CC_LPC, format, ## args)

static uint8_t acpi_cmd;         /* Last received ACPI command */
static uint8_t acpi_addr;        /* First byte of data after ACPI command */
static int acpi_data_count;      /* Number of data writes after command */
static uint8_t acpi_mem_test;    /* Test byte in ACPI memory space */

#ifdef CONFIG_TEMP_SENSOR
static int dptf_temp_sensor_id;			/* last sensor ID written */
static int dptf_temp_threshold;			/* last threshold written */
#endif

/*
 * Deferred function to ensure that ACPI burst mode doesn't remain enabled
 * indefinitely.
 */
static void acpi_unlock_memmap_deferred(void)
{
	lpc_clear_acpi_status_mask(EC_LPC_STATUS_BURST_MODE);
	host_unlock_memmap();
	CPUTS("ACPI force unlock mutex, missed burst disable?");
}
DECLARE_DEFERRED(acpi_unlock_memmap_deferred);

/*
 * Deferred function to lock memmap and to enable LPC interrupts.
 * This is due to LPC interrupt was unable to acquire memmap mutex.
 */
static void deferred_host_lock_memmap(void)
{
	host_lock_memmap();
	lpc_enable_acpi_interrupts();
}
DECLARE_DEFERRED(deferred_host_lock_memmap);

/*
 * This handles AP writes to the EC via the ACPI I/O port. There are only a few
 * ACPI commands (EC_CMD_ACPI_*), but they are all handled here.
 */
int acpi_ap_to_ec(int is_cmd, uint8_t value, uint8_t *resultptr)
{
	int data = 0;
	int retval = 0;
	int result = 0xff;			/* value for bogus read */

	/* Read command/data; this clears the FRMH status bit. */
	if (is_cmd) {
		acpi_cmd = value;
		acpi_data_count = 0;
	} else {
		data = value;
		/*
		 * The first data byte is the ACPI memory address for
		 * read/write commands.
		 */
		if (!acpi_data_count++)
			acpi_addr = data;
	}

	/* Process complete commands */
	if (acpi_cmd == EC_CMD_ACPI_READ && acpi_data_count == 1) {
		/* ACPI read cmd + addr */
		switch (acpi_addr) {
		case EC_ACPI_MEM_VERSION:
			result = EC_ACPI_MEM_VERSION_CURRENT;
			break;
		case EC_ACPI_MEM_TEST:
			result = acpi_mem_test;
			break;
		case EC_ACPI_MEM_TEST_COMPLIMENT:
			result = 0xff - acpi_mem_test;
			break;
#ifdef CONFIG_PWM_KBLIGHT
		case EC_ACPI_MEM_KEYBOARD_BACKLIGHT:
			result = pwm_get_duty(PWM_CH_KBLIGHT);
			break;
#endif
#ifdef CONFIG_FANS
		case EC_ACPI_MEM_FAN_DUTY:
			result = dptf_get_fan_duty_target();
			break;
#endif
#ifdef CONFIG_TEMP_SENSOR
		case EC_ACPI_MEM_TEMP_ID:
			result = dptf_query_next_sensor_event();
			break;
#endif
#ifdef CONFIG_CHARGER
		case EC_ACPI_MEM_CHARGING_LIMIT:
			result = dptf_get_charging_current_limit();
			if (result >= 0)
				result /= EC_ACPI_MEM_CHARGING_LIMIT_STEP_MA;
			else
				result = EC_ACPI_MEM_CHARGING_LIMIT_DISABLED;
			break;
#endif
		default:
			if (acpi_addr >= EC_ACPI_MEM_MAPPED_BEGIN &&
			    acpi_addr <
			    EC_ACPI_MEM_MAPPED_BEGIN + EC_ACPI_MEM_MAPPED_SIZE)
				result = *((uint8_t *)(lpc_get_memmap_range() +
					   acpi_addr -
					   EC_ACPI_MEM_MAPPED_BEGIN));
			else
				CPRINTS("ACPI read 0x%02x (ignored)",
					acpi_addr);
			break;
		}

		/* Send the result byte */
		*resultptr = result;
		retval = 1;

	} else if (acpi_cmd == EC_CMD_ACPI_WRITE && acpi_data_count == 2) {
		/* ACPI write cmd + addr + data */
		switch (acpi_addr) {
		case EC_ACPI_MEM_TEST:
			acpi_mem_test = data;
			break;
#ifdef CONFIG_PWM_KBLIGHT
		case EC_ACPI_MEM_KEYBOARD_BACKLIGHT:
			/*
			 * Debug output with CR not newline, because the host
			 * does a lot of keyboard backlights and it scrolls the
			 * debug console.
			 */
			CPRINTF("\r[%T ACPI kblight %d]", data);
			pwm_set_duty(PWM_CH_KBLIGHT, data);
			break;
#endif
#ifdef CONFIG_FANS
		case EC_ACPI_MEM_FAN_DUTY:
			dptf_set_fan_duty_target(data);
			break;
#endif
#ifdef CONFIG_TEMP_SENSOR
		case EC_ACPI_MEM_TEMP_ID:
			dptf_temp_sensor_id = data;
			break;
		case EC_ACPI_MEM_TEMP_THRESHOLD:
			dptf_temp_threshold = data + EC_TEMP_SENSOR_OFFSET;
			break;
		case EC_ACPI_MEM_TEMP_COMMIT:
		{
			int idx = data & EC_ACPI_MEM_TEMP_COMMIT_SELECT_MASK;
			int enable = data & EC_ACPI_MEM_TEMP_COMMIT_ENABLE_MASK;
			dptf_set_temp_threshold(dptf_temp_sensor_id,
						dptf_temp_threshold,
						idx, enable);
			break;
		}
#endif
#ifdef CONFIG_CHARGER
		case EC_ACPI_MEM_CHARGING_LIMIT:
			if (data == EC_ACPI_MEM_CHARGING_LIMIT_DISABLED) {
				dptf_set_charging_current_limit(-1);
			} else {
				data *= EC_ACPI_MEM_CHARGING_LIMIT_STEP_MA;
				dptf_set_charging_current_limit(data);
			}
			break;
#endif
		default:
			CPRINTS("ACPI write 0x%02x = 0x%02x (ignored)",
				acpi_addr, data);
			break;
		}
	} else if (acpi_cmd == EC_CMD_ACPI_QUERY_EVENT && !acpi_data_count) {
		/* Clear and return the lowest host event */
		int evt_index = lpc_query_host_event_state();
		CPRINTS("ACPI query = %d", evt_index);
		*resultptr = evt_index;
		retval = 1;
	} else if (acpi_cmd == EC_CMD_ACPI_BURST_ENABLE && !acpi_data_count) {
		/*
		 * TODO: The kernel only enables BURST when doing multi-byte
		 * value reads over the ACPI port. We don't do such reads
		 * when our memmap data can be accessed directly over LPC,
		 * so on LM4, for example, this is dead code. We might want
		 * to re-add the CONFIG, now that we have overhead of one
		 * deferred function.
		 */
		if (host_memmap_is_locked()) {
			/*
			 * If already locked by a task, we can not acquire
			 * the mutex and will have to wait in the interrupt
			 * context. But then the task will not get chance to
			 * release the mutex. This will create deadlock
			 * situation. To avoid the deadlock, disable ACPI
			 * interrupts and defer locking.
			 */
			lpc_disable_acpi_interrupts();
			hook_call_deferred(deferred_host_lock_memmap, 0);
		} else {
			host_lock_memmap();
		}
		/* Enter burst mode */
		lpc_set_acpi_status_mask(EC_LPC_STATUS_BURST_MODE);

		/*
		 * Unlock from deferred function in case burst mode is enabled
		 * for an extremely long time  (ex. kernel bug / crash).
		 */
		hook_call_deferred(acpi_unlock_memmap_deferred, 1*SECOND);

		/* ACPI 5.0-12.3.3: Burst ACK */
		*resultptr = 0x90;
		retval = 1;
	} else if (acpi_cmd == EC_CMD_ACPI_BURST_DISABLE && !acpi_data_count) {
		/* Leave burst mode */
		hook_call_deferred(acpi_unlock_memmap_deferred, -1);
		lpc_clear_acpi_status_mask(EC_LPC_STATUS_BURST_MODE);
		host_unlock_memmap();
	}

	return retval;
}
