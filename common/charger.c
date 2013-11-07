/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Common functions for battery charging.
 */

#include "charger.h"
#include "common.h"
#include "console.h"
#include "host_command.h"
#include "printf.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHARGER, outstr)
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)

int charger_closest_voltage(int voltage)
{
	const struct charger_info *info;

	info = charger_get_info();
	return voltage - (voltage % info->voltage_step);
}

int charger_closest_current(int current)
{
	const struct charger_info * const info = charger_get_info();

	/*
	 * If the requested current is non-zero but below our minimum,
	 * return the minimum.  See crosbug.com/p/8662.
	 */
	if (current > 0 && current < info->current_min)
		return info->current_min;

	/* Clip to max */
	if (current > info->current_max)
		return info->current_max;

	/* Otherwise round down to nearest current step */
	return current - (current % info->current_step);
}

static void print_item_name(const char *name)
{
	ccprintf("  %-8s", name);
}

static int check_print_error(int rv)
{
	if (rv == EC_SUCCESS)
		return 1;
	ccputs(rv == EC_ERROR_UNIMPLEMENTED ? "(unsupported)\n" : "(error)\n");
	return 0;
}

static int print_info(void)
{
	int d;
	const struct charger_info *info = charger_get_info();

	/* info */
	print_item_name("Name:");
	ccprintf("%s\n", info->name);

	/* option */
	print_item_name("Option:");
	if (check_print_error(charger_get_option(&d)))
		ccprintf("%016b (0x%04x)\n", d, d);

	/* manufacturer id */
	print_item_name("Man id:");
	if (check_print_error(charger_manufacturer_id(&d)))
		ccprintf("0x%04x\n", d);

	/* device id */
	print_item_name("Dev id:");
	if (check_print_error(charger_device_id(&d)))
		ccprintf("0x%04x\n", d);

	/* charge voltage limit */
	print_item_name("V_batt:");
	if (check_print_error(charger_get_voltage(&d)))
		ccprintf("%5d (%4d - %5d, %3d)\n", d,
			 info->voltage_min, info->voltage_max,
			 info->voltage_step);

	/* charge current limit */
	print_item_name("I_batt:");
	if (check_print_error(charger_get_current(&d)))
		ccprintf("%5d (%4d - %5d, %3d)\n", d,
			 info->current_min, info->current_max,
			 info->current_step);

	/* input current limit */
	print_item_name("I_in:");
	if (check_print_error(charger_get_input_current(&d)))
		ccprintf("%5d (%4d - %5d, %3d)\n", d,
			 info->input_current_min, info->input_current_max,
			 info->input_current_step);

	return EC_SUCCESS;
}

static int command_charger(int argc, char **argv)
{
	int d;
	char *e;

	if (argc != 3)
		return print_info();

	if (strcasecmp(argv[1], "input") == 0) {
		d = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;
		return charger_set_input_current(d);
	} else if (strcasecmp(argv[1], "current") == 0) {
		d = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;
		return charger_set_current(d);
	} else if (strcasecmp(argv[1], "voltage") == 0) {
		d = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;
		return charger_set_voltage(d);
	} else
		return EC_ERROR_PARAM1;
}

DECLARE_CONSOLE_COMMAND(charger, command_charger,
			"[input | current | voltage] [newval]",
			"Get or set charger param(s)",
			NULL);