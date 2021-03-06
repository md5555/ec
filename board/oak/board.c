/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Oak board configuration */

#include "adc_chip.h"
#include "battery.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "driver/temp_sensor/tmp432.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "keyboard_raw.h"
#include "lid_switch.h"
#include "pi3usb9281.h"
#include "power.h"
#include "power_button.h"
#include "registers.h"
#include "spi.h"
#include "switch.h"
#include "task.h"
#include "temp_sensor.h"
#include "temp_sensor_chip.h"
#include "thermal.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

#define GPIO_KB_INPUT  (GPIO_INPUT | GPIO_PULL_UP | GPIO_INT_BOTH)
#define GPIO_KB_OUTPUT GPIO_ODR_HIGH

/* Dispaly port hardware can connect to port 0, 1 or neither. */
#define PD_PORT_NONE -1

static void ap_reset_deferred(void)
{
	/* Warm reset AP */
	chipset_reset(0);
}
DECLARE_DEFERRED(ap_reset_deferred);

void ap_reset_interrupt(enum gpio_signal signal)
{
	if (gpio_get_level(GPIO_AP_RESET_L) == 0)
		hook_call_deferred(ap_reset_deferred, 0);
}

void vbus_wake_interrupt(enum gpio_signal signal)
{
	CPRINTF("VBUS %d\n", !gpio_get_level(signal));
	gpio_set_level(GPIO_USB_PD_VBUS_WAKE,
		       !gpio_get_level(GPIO_VBUS_WAKE_L));
	/*
	 * TODO(crosbug.com/p/41226):
	 *   rev1/rev2 boards don't have vbus input on ec. vbus_wake is a
	 *   logical OR of two vbus status. to workaround the power status
	 *   issue, wake up both pd tasks on vbus_wake interrupt. a proper
	 *   hardware fix will be in rev3.
	 *   enable TCPC POWER_STATUS ALERT1 can solve this issue too.
	 */
	task_wake(TASK_ID_PD_C0);
	task_wake(TASK_ID_PD_C1);
}

void pd_mcu_interrupt(enum gpio_signal signal)
{
#ifdef HAS_TASK_PDCMD
	/* Exchange status with PD MCU to determin interrupt cause */
	host_command_pd_send_status(0);
#endif
}

#include "gpio_list.h"

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_SOC_POWER_GOOD, 1, "POWER_GOOD"},	/* Active high */
	{GPIO_SUSPEND_L, 0, "SUSPEND#_ASSERTED"},	/* Active low */
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* VDC_BOOSTIN_SENSE(PC1): ADC_IN11, output in mV */
	[ADC_VBUS] = {"VBUS", 33000, 4096, 0, STM32_AIN(11)},
	/*
	 * PSYS_MONITOR(PA2): ADC_IN2, 1.44 uA/W on 6.05k Ohm
	 * output in mW
	 */
	[ADC_PSYS] = {"PSYS", 379415, 4096, 0, STM32_AIN(2)},
	/* AMON_BMON(PC0): ADC_IN10, output in uV */
	[ADC_AMON_BMON] = {"AMON_BMON", 183333, 4096, 0, STM32_AIN(10)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"battery", I2C_PORT_BATTERY, 100,  GPIO_I2C0_SCL, GPIO_I2C0_SDA},
	{"pd",      I2C_PORT_PD_MCU,  1000, GPIO_I2C1_SCL, GPIO_I2C1_SDA}
};

const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

struct mutex pericom_mux_lock;
struct pi3usb9281_config pi3usb9281_chips[] = {
	{
		.i2c_port = I2C_PORT_PERICOM,
		.mux_gpio = GPIO_USB_C_BC12_SEL,
		.mux_gpio_level = 0,
		.mux_lock = &pericom_mux_lock,
	},
	{
		.i2c_port = I2C_PORT_PERICOM,
		.mux_gpio = GPIO_USB_C_BC12_SEL,
		.mux_gpio_level = 1,
		.mux_lock = &pericom_mux_lock,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pi3usb9281_chips) ==
	     CONFIG_USB_SWITCH_PI3USB9281_CHIP_COUNT);

/*
 * Temperature sensors data; must be in same order as enum temp_sensor_id.
 * Sensor index and name must match those present in coreboot:
 *     src/mainboard/google/${board}/acpi/dptf.asl
 */
const struct temp_sensor_t temp_sensors[] = {
	{"TMP432_Internal", TEMP_SENSOR_TYPE_BOARD, tmp432_get_val,
		TMP432_IDX_LOCAL, 4},
	{"TMP432_Sensor_1", TEMP_SENSOR_TYPE_BOARD, tmp432_get_val,
		TMP432_IDX_REMOTE1, 4},
	{"TMP432_Sensor_2", TEMP_SENSOR_TYPE_BOARD, tmp432_get_val,
		TMP432_IDX_REMOTE2, 4},
	{"Battery", TEMP_SENSOR_TYPE_BATTERY, charge_temp_sensor_get_val,
		0, 4},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/* Thermal limits for each temp sensor. All temps are in degrees K. Must be in
 * same order as enum temp_sensor_id. To always ignore any temp, use 0.
 */
struct ec_thermal_config thermal_params[] = {
	{{0, 0, 0}, 0, 0}, /* TMP432_Internal */
	{{0, 0, 0}, 0, 0}, /* TMP432_Sensor_1 */
	{{0, 0, 0}, 0, 0}, /* TMP432_Sensor_2 */
	{{0, 0, 0}, 0, 0}, /* Battery Sensor */
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);

struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_COUNT] = {
	{
		.port_addr = 0x54 << 1,
		.driver    = &pi3usb30532_usb_mux_driver,
	},
	{
		.port_addr = 0x55 << 1,
		.driver    = &pi3usb30532_usb_mux_driver,
	},
};

static int discharging_on_ac;

/**
 * Store the state of our USB data switches so that they can be restored
 * after pericom reset.
 */
static int usb_switch_state[CONFIG_USB_PD_PORT_COUNT];
static struct mutex usb_switch_lock[CONFIG_USB_PD_PORT_COUNT];

/**
 * Store the current DP hardware route.
 */
static int dp_hw_port = PD_PORT_NONE;
static struct mutex dp_hw_lock;

/**
 * Discharge battery when on AC power for factory test.
 */
int board_discharge_on_ac(int enable)
{
	int rv = charger_discharge_on_ac(enable);

	if (rv == EC_SUCCESS)
		discharging_on_ac = enable;

	return rv;
}

/**
 * Reset PD MCU
 */
void board_reset_pd_mcu(void)
{
	gpio_set_level(GPIO_USB_PD_RST_L, 0);
	usleep(100);
	gpio_set_level(GPIO_USB_PD_RST_L, 1);
}

void __board_i2c_set_timeout(int port, uint32_t timeout)
{
}

void i2c_set_timeout(int port, uint32_t timeout)
		__attribute__((weak, alias("__board_i2c_set_timeout")));

/* Initialize board. */
static void board_init(void)
{
	int i, bc12_status;
	struct charge_port_info charge_none, charge_vbus;
	struct charge_port_info *charge_sel;

	/* Enable rev1 testing GPIOs */
	gpio_set_level(GPIO_SYSTEM_POWER_H, 1);
	/* Enable PD MCU interrupt */
	gpio_enable_interrupt(GPIO_PD_MCU_INT);
	/* Enable VBUS interrupt */
	gpio_enable_interrupt(GPIO_VBUS_WAKE_L);
#ifdef CONFIG_AP_WARM_RESET_INTERRUPT
	gpio_enable_interrupt(GPIO_AP_RESET_L);
#endif

	charge_none.voltage = USB_CHARGER_VOLTAGE_MV;
	charge_none.current = 0;
	charge_vbus.voltage = USB_CHARGER_VOLTAGE_MV;
	charge_vbus.current = USB_CHARGER_MIN_CURR_MA;
	for (i = 0; i < CONFIG_USB_PD_PORT_COUNT; i++) {
		/* Initialize all pericom charge suppliers to 0 */
		charge_manager_update_charge(
				CHARGE_SUPPLIER_PROPRIETARY,
				i,
				&charge_none);
		charge_manager_update_charge(
				CHARGE_SUPPLIER_BC12_CDP,
				i,
				&charge_none);
		charge_manager_update_charge(
				CHARGE_SUPPLIER_BC12_DCP,
				i,
				&charge_none);
		charge_manager_update_charge(
				CHARGE_SUPPLIER_BC12_SDP,
				i,
				&charge_none);
		charge_manager_update_charge(
				CHARGE_SUPPLIER_OTHER,
				i,
				&charge_none);

		/* Initialize VBUS supplier based on VBUS */
		/* TODO(crbug.com/498974): Don't do i2c from hook_init. */
		bc12_status = pi3usb9281_get_charger_status(i);
		charge_sel = PI3USB9281_CHG_STATUS_ANY(bc12_status) ?
				&charge_vbus : &charge_none;
		charge_manager_update_charge(
				CHARGE_SUPPLIER_VBUS,
				i,
				charge_sel);
	}
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/**
 * Set active charge port -- only one port can active at a time.
 *
 * @param charge_port    Charge port to enable.
 *
 * Return EC_SUCCESS if charge port is accepted and made active.
 * EC_ERROR_* otherwise.
 */
int board_set_active_charge_port(int charge_port)
{
	/* charge port is a physical port */
	int is_real_port = (charge_port >= 0 &&
			    charge_port < CONFIG_USB_PD_PORT_COUNT);
	/* check if we are source VBUS on the port */
	int source = gpio_get_level(charge_port == 0 ? GPIO_USB_C0_5V_OUT :
						       GPIO_USB_C1_5V_OUT);

	if (is_real_port && source) {
		CPRINTF("Skip enable p%d", charge_port);
		return EC_ERROR_INVAL;
	}

	CPRINTF("New chg p%d", charge_port);

	if (charge_port == CHARGE_PORT_NONE) {
		/*
		 * TODO: currently we only get VBUS knowledge when charge
		 * is enabled. so, when not changing, we need to enable
		 * both ports. but, this is dangerous if you have two
		 * chargers plugged in and you set charge override to -1
		 * then it will enable both sides!
		 */
		gpio_set_level(GPIO_USB_C0_CHARGE_L, 0);
		gpio_set_level(GPIO_USB_C1_CHARGE_L, 0);
	} else {
		/* Make sure non-charging port is disabled */
		gpio_set_level(charge_port ? GPIO_USB_C0_CHARGE_L :
					     GPIO_USB_C1_CHARGE_L, 1);
		/* Enable charging port */
		gpio_set_level(charge_port ? GPIO_USB_C1_CHARGE_L :
					     GPIO_USB_C0_CHARGE_L, 0);
	}

	return EC_SUCCESS;
}

/**
 * Set the charge limit based upon desired maximum.
 *
 * @param charge_ma     Desired charge limit (mA).
 */
void board_set_charge_limit(int charge_ma)
{
	charge_set_input_current_limit(MAX(charge_ma,
					   CONFIG_CHARGER_INPUT_CURRENT));
}

/**
 * Set type-C port USB2.0 switch state.
 *
 * @param port       the type-C port to change
 * @param setting    enum usb_switch
 */
void board_set_usb_switches(int port, enum usb_switch setting)
{
	/* If switch is not charging, then return */
	if (setting == usb_switch_state[port])
		return;

	mutex_lock(&usb_switch_lock[port]);
	if (setting != USB_SWITCH_RESTORE)
		usb_switch_state[port] = setting;
	pi3usb9281_set_switches(port, usb_switch_state[port]);
	mutex_unlock(&usb_switch_lock[port]);
}

static void hpd_irq_deferred(void)
{
	gpio_set_level(GPIO_USB_DP_HPD, 1);
}
DECLARE_DEFERRED(hpd_irq_deferred);

/**
 * Turn on DP hardware on type-C port.
 */
void board_typec_dp_on(int port)
{
	mutex_lock(&dp_hw_lock);

	if (dp_hw_port != !port) {
		/* Get control of DP hardware */
		dp_hw_port = port;
		gpio_set_level(GPIO_DP_SWITCH_CTL, port);

		if (!gpio_get_level(GPIO_USB_DP_HPD)) {
			gpio_set_level(GPIO_USB_DP_HPD, 1);
		} else {
			gpio_set_level(GPIO_USB_DP_HPD, 0);
			hook_call_deferred(hpd_irq_deferred,
					HPD_DSTREAM_DEBOUNCE_IRQ);
		}
	}

	mutex_unlock(&dp_hw_lock);
}

/**
 * Turn off a PD port's DP output.
 */
void board_typec_dp_off(int port, int *dp_flags)
{
	mutex_lock(&dp_hw_lock);

	if (dp_hw_port == !port) {
		mutex_unlock(&dp_hw_lock);
		return;
	}

	dp_hw_port = PD_PORT_NONE;
	gpio_set_level(GPIO_USB_DP_HPD, 0);
	mutex_unlock(&dp_hw_lock);

	/* Enable the other port if its dp flag is on */
	if (dp_flags[!port] & DP_FLAGS_DP_ON)
		board_typec_dp_on(!port);
}

/**
 * Set DP hotplug detect level.
 */
void board_typec_dp_set(int port, int level)
{
	mutex_lock(&dp_hw_lock);

	if (dp_hw_port == PD_PORT_NONE) {
		dp_hw_port = port;
		gpio_set_level(GPIO_DP_SWITCH_CTL, port);
	}

	if (dp_hw_port == port)
		gpio_set_level(GPIO_USB_DP_HPD, level);

	mutex_unlock(&dp_hw_lock);
}

#ifndef CONFIG_AP_WARM_RESET_INTERRUPT
/* Using this hook if system doesn't have enough external line. */
static void check_ap_reset_second(void)
{
	/* Check the warm reset signal from servo board */
	static int warm_reset, last;

	warm_reset = !gpio_get_level(GPIO_AP_RESET_L);

	if (last == warm_reset)
		return;

	if (warm_reset)
		chipset_reset(0); /* Warm reset AP */

	last = warm_reset;
}
DECLARE_HOOK(HOOK_SECOND, check_ap_reset_second, HOOK_PRIO_DEFAULT);
#endif
