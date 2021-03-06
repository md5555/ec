/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charger.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb.h"
#include "usb_mux.h"
#include "usb_pd.h"


#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

#define PDO_FIXED_FLAGS (PDO_FIXED_EXTERNAL | PDO_FIXED_DATA_SWAP)

/* Voltage indexes for the PDOs */
enum volt_idx {
	PDO_IDX_5V  = 0,
	PDO_IDX_12V = 1,
	PDO_IDX_20V = 2,

	PDO_IDX_COUNT
};

const uint32_t pd_src_pdo[] = {
	[PDO_IDX_5V]  = PDO_FIXED(5000,  3000, PDO_FIXED_FLAGS),
	[PDO_IDX_12V] = PDO_FIXED(12000, 3000, PDO_FIXED_FLAGS),
	[PDO_IDX_20V] = PDO_FIXED(20000, 3000, PDO_FIXED_FLAGS),
};
const int pd_src_pdo_cnt = ARRAY_SIZE(pd_src_pdo);
BUILD_ASSERT(ARRAY_SIZE(pd_src_pdo) == PDO_IDX_COUNT);

/* Holds valid object position (opos) for entered mode */
static int alt_mode[PD_AMODE_COUNT];

void pd_set_input_current_limit(int port, uint32_t max_ma,
				uint32_t supply_voltage)
{

}


int pd_is_valid_input_voltage(int mv)
{
	/* Any voltage less than the max is allowed */
	return 1;
}

int pd_check_requested_voltage(uint32_t rdo)
{
	int max_ma = rdo & 0x3FF;
	int op_ma = (rdo >> 10) & 0x3FF;
	int idx = rdo >> 28;
	uint32_t pdo;
	uint32_t pdo_ma;

	if (!idx || idx > pd_src_pdo_cnt)
		return EC_ERROR_INVAL; /* Invalid index */

	/* check current ... */
	pdo = pd_src_pdo[idx - 1];
	pdo_ma = (pdo & 0x3ff);
	if (op_ma > pdo_ma)
		return EC_ERROR_INVAL; /* too much op current */
	if (max_ma > pdo_ma)
		return EC_ERROR_INVAL; /* too much max current */

	CPRINTF("Requested %d V %d mA (for %d/%d mA)\n",
		 ((pdo >> 10) & 0x3ff) * 50, (pdo & 0x3ff) * 10,
		 ((rdo >> 10) & 0x3ff) * 10, (rdo & 0x3ff) * 10);

	return EC_SUCCESS;
}

void pd_transition_voltage(int idx)
{
	/* PDO index are starting from 1 */
	switch (idx - 1) {
	case PDO_IDX_20V:
#if 0
		gpio_set_level(GPIO_PPVAR_VBUS_EN, 0);
		gpio_set_level(GPIO_PP20000_EN, 1);
		break;
#endif /* 20V transition is broken, putting 12V instead: fall-through */
	case PDO_IDX_12V:
		gpio_set_level(GPIO_PP12000_EN, 1);
		gpio_set_level(GPIO_PP20000_EN, 0);
		gpio_set_level(GPIO_PPVAR_VBUS_EN, 1);
		break;
	case PDO_IDX_5V:
	default:
		gpio_set_level(GPIO_PP12000_EN, 0);
		gpio_set_level(GPIO_PP20000_EN, 0);
		gpio_set_level(GPIO_PPVAR_VBUS_EN, 1);
	}
}

int pd_set_power_supply_ready(int port)
{
	/* provide VBUS */
	gpio_set_level(GPIO_PPVAR_VBUS_EN, 1);
	return EC_SUCCESS; /* we are ready */
}

void pd_power_supply_reset(int port)
{
	/* Kill VBUS */
	gpio_set_level(GPIO_PPVAR_VBUS_EN, 0);
	gpio_set_level(GPIO_PP12000_EN, 0);
	gpio_set_level(GPIO_PP20000_EN, 0);
}

int pd_snk_is_vbus_provided(int port)
{
	return 0;
}

int pd_board_checks(void)
{
	return EC_SUCCESS;
}

int pd_check_power_swap(int port)
{
	/* We are source-only */
	return 0;
}

int pd_check_data_swap(int port, int data_role)
{
	/* Always allow data swap */
	return 1;
}

void pd_execute_data_swap(int port, int data_role)
{
	/* Do nothing */
}

void pd_check_pr_role(int port, int pr_role, int flags)
{
}

void pd_check_dr_role(int port, int dr_role, int flags)
{
}

int pd_alt_mode(int port, uint16_t svid)
{
	if (svid == USB_SID_DISPLAYPORT)
		return alt_mode[PD_AMODE_DISPLAYPORT];
	else if (svid == USB_VID_GOOGLE)
		return alt_mode[PD_AMODE_GOOGLE];
	return 0;
}

/* ----------------- Vendor Defined Messages ------------------ */
const uint32_t vdo_idh = VDO_IDH(0, /* data caps as USB host */
				 1, /* data caps as USB device */
				 IDH_PTYPE_AMA, /* Alternate mode */
				 1, /* supports alt modes */
				 USB_VID_GOOGLE);

const uint32_t vdo_product = VDO_PRODUCT(CONFIG_USB_PID, CONFIG_USB_BCD_DEV);

const uint32_t vdo_ama = VDO_AMA(CONFIG_USB_PD_IDENTITY_HW_VERS,
				 CONFIG_USB_PD_IDENTITY_SW_VERS,
				 0, 0, 0, 0, /* SS[TR][12] */
				 0, /* Vconn power */
				 0, /* Vconn power required */
				 0, /* Vbus power required */
				 AMA_USBSS_U31_GEN1 /* USB SS support */);

static int svdm_response_identity(int port, uint32_t *payload)
{
	payload[VDO_I(IDH)] = vdo_idh;
	payload[VDO_I(CSTAT)] = VDO_CSTAT(0);
	payload[VDO_I(PRODUCT)] = vdo_product;
	payload[VDO_I(AMA)] = vdo_ama;
	return VDO_I(AMA) + 1;
}

static int svdm_response_svids(int port, uint32_t *payload)
{
	payload[1] = VDO_SVID(USB_SID_DISPLAYPORT, USB_VID_GOOGLE);
	payload[2] = 0;
	return 3;
}

#define OPOS_DP 1
#define OPOS_GFU 1

const uint32_t vdo_dp_modes[1] =  {
	VDO_MODE_DP(0,		   /* UFP pin cfg supported : none */
		    MODE_DP_PIN_C | MODE_DP_PIN_D, /* DFP pin cfg supported */
		    0,		   /* usb2.0 signalling even in AMode */
		    CABLE_PLUG,    /* its a plug */
		    MODE_DP_V13,   /* DPv1.3 Support, no Gen2 */
		    MODE_DP_SNK)   /* Its a sink only */
};

const uint32_t vdo_goog_modes[1] =  {
	VDO_MODE_GOOGLE(MODE_GOOGLE_FU)
};

static int svdm_response_modes(int port, uint32_t *payload)
{
	if (PD_VDO_VID(payload[0]) == USB_SID_DISPLAYPORT) {
		memcpy(payload + 1, vdo_dp_modes, sizeof(vdo_dp_modes));
		return ARRAY_SIZE(vdo_dp_modes) + 1;
	} else if (PD_VDO_VID(payload[0]) == USB_VID_GOOGLE) {
		memcpy(payload + 1, vdo_goog_modes, sizeof(vdo_goog_modes));
		return ARRAY_SIZE(vdo_goog_modes) + 1;
	} else {
		return 0; /* nak */
	}
}

static int dp_status(int port, uint32_t *payload)
{
	int opos = PD_VDO_OPOS(payload[0]);
	int hpd = gpio_get_level(GPIO_DP_HPD);
	if (opos != OPOS_DP)
		return 0; /* nak */

	payload[1] = VDO_DP_STATUS(0,                /* IRQ_HPD */
				   (hpd == 1),       /* HPD_HI|LOW */
				   0,		     /* request exit DP */
				   0,		     /* request exit USB */
				   1,		     /* MF pref */
				   gpio_get_level(GPIO_PD_SBU_ENABLE),
				   0,		     /* power low */
				   0x2);
	return 2;
}

static int dp_config(int port, uint32_t *payload)
{
	/* is it a 2+2 or 4 DP lanes mode ? */
	enum typec_mux mux = PD_DP_CFG_PIN(payload[1]) & MODE_DP_PIN_MF_MASK ?
				TYPEC_MUX_DOCK : TYPEC_MUX_DP;

	if (PD_DP_CFG_DPON(payload[1]))
		gpio_set_level(GPIO_PD_SBU_ENABLE, 1);
	/* Get the DP lanes (or DP+USB SS depending on the mode) */
	usb_mux_set(port, mux, USB_SWITCH_CONNECT, pd_get_polarity(port));

	return 1;
}

static int svdm_enter_mode(int port, uint32_t *payload)
{
	int rv = 0; /* will generate a NAK */

	/* SID & mode request is valid */
	if ((PD_VDO_VID(payload[0]) == USB_SID_DISPLAYPORT) &&
	    (PD_VDO_OPOS(payload[0]) == OPOS_DP)) {
		alt_mode[PD_AMODE_DISPLAYPORT] = OPOS_DP;
		rv = 1;
		pd_log_event(PD_EVENT_VIDEO_DP_MODE, 0, 1, NULL);
	} else if ((PD_VDO_VID(payload[0]) == USB_VID_GOOGLE) &&
		   (PD_VDO_OPOS(payload[0]) == OPOS_GFU)) {
		alt_mode[PD_AMODE_GOOGLE] = OPOS_GFU;
		rv = 1;
	}

	if (rv)
		/*
		 * If we failed initial mode entry we'll have enumerated the USB
		 * Billboard class.  If so we should disconnect.
		 */
		usb_disconnect();

	return rv;
}

static int svdm_exit_mode(int port, uint32_t *payload)
{
	if (PD_VDO_VID(payload[0]) == USB_SID_DISPLAYPORT) {
		gpio_set_level(GPIO_PD_SBU_ENABLE, 0);
		alt_mode[PD_AMODE_DISPLAYPORT] = 0;
		pd_log_event(PD_EVENT_VIDEO_DP_MODE, 0, 0, NULL);
	} else if (PD_VDO_VID(payload[0]) == USB_VID_GOOGLE) {
		alt_mode[PD_AMODE_GOOGLE] = 0;
	} else {
		CPRINTF("Unknown exit mode req:0x%08x\n", payload[0]);
	}

	return 1; /* Must return ACK */
}

static struct amode_fx dp_fx = {
	.status = &dp_status,
	.config = &dp_config,
};

const struct svdm_response svdm_rsp = {
	.identity = &svdm_response_identity,
	.svids = &svdm_response_svids,
	.modes = &svdm_response_modes,
	.enter_mode = &svdm_enter_mode,
	.amode = &dp_fx,
	.exit_mode = &svdm_exit_mode,
};

int pd_custom_vdm(int port, int cnt, uint32_t *payload,
		  uint32_t **rpayload)
{
	int rsize;

	if (PD_VDO_VID(payload[0]) != USB_VID_GOOGLE ||
	    !alt_mode[PD_AMODE_GOOGLE])
		return 0;

	*rpayload = payload;

	rsize = pd_custom_flash_vdm(port, cnt, payload);
	if (!rsize) {
		int cmd = PD_VDO_CMD(payload[0]);
		switch (cmd) {
#ifdef CONFIG_USB_PD_LOGGING
		case VDO_CMD_GET_LOG:
			rsize = pd_vdm_get_log_entry(payload);
			break;
#endif
		default:
			/* Unknown : do not answer */
			return 0;
		}
	}

	/* respond (positively) to the request */
	payload[0] |= VDO_SRC_RESPONDER;

	return rsize;
}

