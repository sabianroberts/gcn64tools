/*	gc_n64_usb : Gamecube or N64 controller to USB adapter firmware
	Copyright (C) 2007-2015  Raphael Assenat <raph@raphnet.net>

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "raphnetadapter.h"
#include "gcn64_priv.h"
#include "gcn64lib.h"
#include "requests.h"

#include "hidapi.h"

static int dusbr_verbose = 0;

#define IS_VERBOSE()	(dusbr_verbose)

struct supported_adapter {
	uint16_t vid, pid;
	int if_number; // Set to -1 for "display only" (no command interface)
	struct rnt_adap_caps caps;
};

#define RNT_V3_STD	(RNTF_FW_UPDATE | RNTF_POLL_RATE | RNTF_SUSPEND_POLLING | RNTF_CONTROLLER_TYPE)

static struct supported_adapter supported_adapters[] = {
	/* vid, pid, if_no, { rpsize, n_channels, n_raw_channels, features } */

	{ OUR_VENDOR_ID, 0x0017, 1, { 0, 1, 1, RNT_V3_STD | RNTF_GC_FULL_SLIDERS | RNTF_GC_INVERT_TRIG } }, // GC/N64 USB v3.0, 3.1.0, 3.1.1
	{ OUR_VENDOR_ID, 0x001D, 1, { 0, 1, 1, RNT_V3_STD | RNTF_GC_FULL_SLIDERS | RNTF_GC_INVERT_TRIG } }, // GC/N64 USB v3.2.0 ... v3.3.x
	{ OUR_VENDOR_ID, 0x0020, 1, { 0, 1, 1, RNT_V3_STD | RNTF_GC_FULL_SLIDERS | RNTF_GC_INVERT_TRIG } }, // GCN64->USB v3.2.1 (N64 mode)
	{ OUR_VENDOR_ID, 0x0021, 1, { 0, 1, 1, RNT_V3_STD | RNTF_GC_FULL_SLIDERS | RNTF_GC_INVERT_TRIG } }, // GCN64->USB v3.2.1 (GC mode)
	{ OUR_VENDOR_ID, 0x0022, 1, { 0, 2, 2, RNT_V3_STD | RNTF_GC_FULL_SLIDERS | RNTF_GC_INVERT_TRIG } }, // GCN64->USB v3.3.x (2x GC/N64 mode)
	{ OUR_VENDOR_ID, 0x0030, 1, { 0, 2, 2, RNT_V3_STD | RNTF_GC_FULL_SLIDERS | RNTF_GC_INVERT_TRIG } }, // GCN64->USB v3.3.0 (2x N64-only mode)
	{ OUR_VENDOR_ID, 0x0031, 1, { 0, 2, 2, RNT_V3_STD | RNTF_GC_FULL_SLIDERS | RNTF_GC_INVERT_TRIG } }, // GCN64->USB v3.3.0 (2x GC-only mode)

	{ OUR_VENDOR_ID, 0x0032, 1, { 0, 1, 1, RNT_V3_STD | RNTF_BLOCK_IO | RNTF_GC_FULL_SLIDERS | RNTF_GC_INVERT_TRIG } }, // GC/N64 USB v3.4.x (GC/N64 mode)
	{ OUR_VENDOR_ID, 0x0033, 1, { 0, 1, 1, RNT_V3_STD | RNTF_BLOCK_IO | RNTF_GC_FULL_SLIDERS | RNTF_GC_INVERT_TRIG } }, // GC/N64 USB v3.4.x (N64 mode)
	{ OUR_VENDOR_ID, 0x0034, 1, { 0, 1, 1, RNT_V3_STD | RNTF_BLOCK_IO | RNTF_GC_FULL_SLIDERS | RNTF_GC_INVERT_TRIG } }, // GC/N64 USB v3.4.x (GC mode)
	{ OUR_VENDOR_ID, 0x0035, 1, { 0, 2, 2, RNT_V3_STD | RNTF_BLOCK_IO | RNTF_GC_FULL_SLIDERS | RNTF_GC_INVERT_TRIG } }, // GC/N64 USB v3.4.x (2x GC/N64 mode)
	{ OUR_VENDOR_ID, 0x0036, 1, { 0, 2, 2, RNT_V3_STD | RNTF_BLOCK_IO | RNTF_GC_FULL_SLIDERS | RNTF_GC_INVERT_TRIG } }, // GC/N64 USB v3.4.x (2x N64-only mode)
	{ OUR_VENDOR_ID, 0x0037, 1, { 0, 2, 2, RNT_V3_STD | RNTF_BLOCK_IO | RNTF_GC_FULL_SLIDERS | RNTF_GC_INVERT_TRIG } }, // GC/N64 USB v3.4.x (2x GC-only mode)

	// For future use...
	{ OUR_VENDOR_ID, 0x0038, 1, { 0, 2, 2, RNT_V3_STD | RNTF_BLOCK_IO } },
	{ OUR_VENDOR_ID, 0x0039, 1, { 0, 2, 2, RNT_V3_STD | RNTF_BLOCK_IO } },
	{ OUR_VENDOR_ID, 0x003A, 1, { 0, 2, 2, RNT_V3_STD | RNTF_BLOCK_IO } },
	{ OUR_VENDOR_ID, 0x003B, 1, { 0, 2, 2, RNT_V3_STD | RNTF_BLOCK_IO } },
	{ OUR_VENDOR_ID, 0x003C, 1, { 0, 2, 2, RNT_V3_STD | RNTF_BLOCK_IO } },
	{ OUR_VENDOR_ID, 0x003D, 1, { 0, 2, 2, RNT_V3_STD | RNTF_BLOCK_IO } },
	{ OUR_VENDOR_ID, 0x003E, 1, { 0, 2, 2, RNT_V3_STD | RNTF_BLOCK_IO } },
	{ OUR_VENDOR_ID, 0x003F, 1, { 0, 2, 2, RNT_V3_STD | RNTF_BLOCK_IO } },

	{ OUR_VENDOR_ID, 0x0050, 1, { 63, 1, 0, RNT_V3_STD | RNTF_DPAD_AS_BUTTONS } }, // PC Engine to USB v1.0.0
	{ OUR_VENDOR_ID, 0x0051, 1, { 63, 5, 0, RNT_V3_STD | RNTF_DPAD_AS_BUTTONS } }, // PC Engine to USB v1.0.0 (5 player mode)

	// Legacy devices (vusb, non-upgradeable and typically without configurable features)
	{ OUR_VENDOR_ID, 0x0003, -1, { 0, 4 } }, // 4nes4snes 1.4.2, 1.5
	{ 0x288B, 0x0003, -1 }, // 4nes4snes 1.4.1 (wrong vendor id)
	{ OUR_VENDOR_ID, 0x0004, -1, }, // GCN64->USB v2.3
	{ OUR_VENDOR_ID, 0x0005, -1, }, // Saturn to USB adapter (joystick mode)
	{ OUR_VENDOR_ID, 0x0006, -1, }, // Saturn to USB adapter (mouse mode)
	{ OUR_VENDOR_ID, 0x0007, -1, }, // Famicom controller to USB adapter
	{ OUR_VENDOR_ID, 0x0008, -1, }, // Dreamcast to USB adapter (joystick mode)
	{ OUR_VENDOR_ID, 0x0009, -1, }, // Dreamcast to USB adapter (mouse mode)
	{ OUR_VENDOR_ID, 0x000A, -1, }, // Dreamcast to USB adapter (keyboard mode)
	{ OUR_VENDOR_ID, 0x000B, -1, }, // GCN64->USB v2.9 (gamecube keyboard mode)
	{ OUR_VENDOR_ID, 0x000C, -1, }, // GCN64->USB v2.9 (joystick mode)
	{ OUR_VENDOR_ID, 0x000D, -1, }, // GCN64->USB v2.9 (keyboard mode)
	{ OUR_VENDOR_ID, 0x000E, -1, }, // Virtual boy to USB v1.1
	{ OUR_VENDOR_ID, 0x000F, -1, }, // GCN64->USB v2.9 (N64 to USB--Special custom version)
	{ OUR_VENDOR_ID, 0x0010, -1, }, // WUSBMote v1.2 (Joystick mode)
	{ OUR_VENDOR_ID, 0x0011, -1, }, // WUSBMote v1.2 (Mouse mode)
	{ OUR_VENDOR_ID, 0x0012, -1, }, // WUSBMote v1.2.1 (Joystick mode)
	{ OUR_VENDOR_ID, 0x0013, -1, }, // WUSBMote v1.2.1 (Mouse mode)
	{ OUR_VENDOR_ID, 0x0014, -1, }, // WUSBMote v1.3 (Joystick mode)
	{ OUR_VENDOR_ID, 0x0015, -1, }, // WUSBMote v1.3 (Mouse mode)
	{ OUR_VENDOR_ID, 0x0016, -1, }, // WUSBMote v1.3 (I2C interface mode)
	{ OUR_VENDOR_ID, 0x0018, -1, }, // Atari Jaguar controller to USB v1.1
	{ OUR_VENDOR_ID, 0x0019, -1, }, // MultiDB9 Adapter
	{ OUR_VENDOR_ID, 0x001A, -1, }, // MultiDB9 ADapter (multitap mode)
	{ OUR_VENDOR_ID, 0x001B, -1, }, // USB Game12 v1.1
	{ OUR_VENDOR_ID, 0x001E, -1, }, // Vectrex to USB adapter
	{ OUR_VENDOR_ID, 0x0023, -1, }, // 3DO controller to USB adapter
	{ OUR_VENDOR_ID, 0x0024, -1, }, // Intellivision to USB adapter (v1.3)
	{ OUR_VENDOR_ID, 0x0025, -1, }, // CD32 controller to USB adapter

	{ }, // terminator
};

int rnt_init(int verbose)
{
	dusbr_verbose = verbose;
	hid_init();
	return 0;
}

void rnt_shutdown(void)
{
	hid_exit();
}

#define PID_NOT_HANDLED		0
#define PID_HANDLED			1
#define PID_HANDLED_LEGACY	2
static char isProductIdHandled(unsigned short pid, int interface_number, struct rnt_adap_caps *caps)
{
	int i;

	for (i=0; supported_adapters[i].vid; i++) {
		if (pid == supported_adapters[i].pid) {
			if (interface_number == supported_adapters[i].if_number || supported_adapters[i].if_number == -1) {
				if (caps) {
					memcpy(caps, &supported_adapters[i].caps, sizeof (struct rnt_adap_caps));
				}
				return supported_adapters[i].if_number == -1 ? PID_HANDLED_LEGACY : PID_HANDLED;
			}
		}
	}

	return PID_NOT_HANDLED;
}

struct rnt_adap_list_ctx *rnt_allocListCtx(void)
{
	struct rnt_adap_list_ctx *ctx;
	ctx = calloc(1, sizeof(struct rnt_adap_list_ctx));
	return ctx;
}

void rnt_freeListCtx(struct rnt_adap_list_ctx *ctx)
{
	if (ctx) {
		if (ctx->devs) {
			hid_free_enumeration(ctx->devs);
		}
		free(ctx);
	}
}

int rnt_countDevices(void)
{
	struct rnt_adap_list_ctx *ctx;
	struct rnt_adap_info inf;
	int count = 0;

	ctx = rnt_allocListCtx();
	while (gcn64_listDevices(&inf, ctx)) {
		count++;
	}
	rnt_freeListCtx(ctx);

	return count;
}

/**
 * \brief List instances of our rgbleds device on the USB busses.
 * \param info Pointer to rnt_adap_info structure to store data
 * \param dst Destination buffer for device serial number/id.
 * \param dstbuf_size Destination buffer size.
 */
struct rnt_adap_info *gcn64_listDevices(struct rnt_adap_info *info, struct rnt_adap_list_ctx *ctx)
{
	struct rnt_adap_caps caps;
	int handled;

	memset(info, 0, sizeof(struct rnt_adap_info));

	if (!ctx) {
		fprintf(stderr, "gcn64_listDevices: Passed null context\n");
		return NULL;
	}

	if (ctx->devs)
		goto jumpin;

	if (IS_VERBOSE()) {
		printf("Start listing\n");
	}

	ctx->devs = hid_enumerate(OUR_VENDOR_ID, 0x0000);
	if (!ctx->devs) {
		printf("Hid enumerate returned NULL\n");
		return NULL;
	}

	for (ctx->cur_dev = ctx->devs; ctx->cur_dev; ctx->cur_dev = ctx->cur_dev->next)
	{
		if (IS_VERBOSE()) {
			printf("Considering 0x%04x:0x%04x\n", ctx->cur_dev->vendor_id, ctx->cur_dev->product_id);
		}
		handled = isProductIdHandled(ctx->cur_dev->product_id, ctx->cur_dev->interface_number, &caps);
		if (handled != PID_NOT_HANDLED)
		{
				info->usb_vid = ctx->cur_dev->vendor_id;
				info->usb_pid = ctx->cur_dev->product_id;
				info->version_major = ctx->cur_dev->release_number >> 8;
				info->version_minor = ctx->cur_dev->release_number & 0xff;
				wcsncpy(info->str_prodname, ctx->cur_dev->product_string, PRODNAME_MAXCHARS-1);
				wcsncpy(info->str_serial, ctx->cur_dev->serial_number, SERIAL_MAXCHARS-1);
				strncpy(info->str_path, ctx->cur_dev->path, PATH_MAXCHARS-1);
				memcpy(&info->caps, &caps, sizeof(info->caps));
				if (handled == PID_HANDLED_LEGACY) {
					info->legacy_adapter = 1;
				}
				return info;
		}

		jumpin:
		// prevent 'error: label at end of compound statement'
		continue;
	}

	return NULL;
}

rnt_hdl_t rnt_openDevice(struct rnt_adap_info *dev)
{
	hid_device *hdev;
	rnt_hdl_t hdl;
	char version[64];

	if (!dev)
		return NULL;

	hdl = calloc(1, sizeof(struct _rnt_hdl_t));
	if (!hdl) {
		perror("malloc");
		return NULL;
	}

	// Legacy devices (raphnet products based on V-USB) do not have
	// an hid data interface. Those adapters cannot be managed/configures.
	//
	// But we can still "open" them, but only to display their USB VID/PID
	// and name.
	if (!dev->legacy_adapter) {
		if (IS_VERBOSE()) {
			printf("Opening device path: '%s'\n", dev->str_path);
		}

		hdev = hid_open_path(dev->str_path);
		if (!hdev) {
			free(hdl);
			return NULL;
		}

		hdl->hdev = hdev;
	}

	hdl->version_major = dev->version_major;
	hdl->version_minor = dev->version_minor;
	hdl->report_size = dev->caps.rpsize ? dev->caps.rpsize : 63;

	if (!(dev->caps.features & RNTF_BLOCK_IO) && !dev->caps.rpsize) {
		printf("Pre-3.4 version detected. Setting report size to 40 bytes\n");
		hdl->report_size = 40;
	}

	if (0 == rnt_getVersion(hdl, version, sizeof(version))) {
		int a,b,c;

		if (3 == sscanf(version, "%d.%d.%d", &a, &b, &c)) {
			if ((a >= 3) && (b >= 4) && (c > 0)) {
				dev->caps.features |= RNTF_TRIGGER_AS_BUTTONS;
			}
		}
	}

	memcpy(&hdl->caps, &dev->caps, sizeof(hdl->caps));

	return hdl;
}

rnt_hdl_t rnt_openBy(struct rnt_adap_info *dev, unsigned char flags)
{
	struct rnt_adap_list_ctx *ctx;
	struct rnt_adap_info inf;
	rnt_hdl_t h;

	if (IS_VERBOSE())
		printf("rnt_openBy, flags=0x%02x\n", flags);

	ctx = rnt_allocListCtx();
	if (!ctx)
		return NULL;

	while (gcn64_listDevices(&inf, ctx)) {
		if (IS_VERBOSE())
			printf("Considering '%s'\n", inf.str_path);

		if (flags & GCN64_FLG_OPEN_BY_SERIAL) {
			if (wcscmp(inf.str_serial, dev->str_serial))
				continue;
		}

		if (flags & GCN64_FLG_OPEN_BY_PATH) {
			if (strcmp(inf.str_path, dev->str_path))
				continue;
		}

		if (flags & GCN64_FLG_OPEN_BY_VID) {
			if (inf.usb_vid != dev->usb_vid)
				continue;
		}

		if (flags & GCN64_FLG_OPEN_BY_PID) {
			if (inf.usb_pid != dev->usb_pid)
				continue;
		}

		if (IS_VERBOSE())
			printf("Found device. opening...\n");

		h = rnt_openDevice(&inf);
		rnt_freeListCtx(ctx);
		return h;
	}

	rnt_freeListCtx(ctx);
	return NULL;
}

void rnt_closeDevice(rnt_hdl_t hdl)
{
	hid_device *hdev = hdl->hdev;

	if (hdev) {
		hid_close(hdev);
	}

	free(hdl);
}

int rnt_send_cmd(rnt_hdl_t hdl, const unsigned char *cmd, int cmdlen)
{
	hid_device *hdev = hdl->hdev;
	unsigned char buffer[hdl->report_size+1];
	int n;

	if (!hdev) {
		return -1;
	}

	if (cmdlen > (sizeof(buffer)-1)) {
		fprintf(stderr, "Error: Command too long\n");
		return -1;
	}

	memset(buffer, 0, sizeof(buffer));

	buffer[0] = 0x00; // report ID set to 0 (device has only one)
	memcpy(buffer + 1, cmd, cmdlen);

	n = hid_send_feature_report(hdev, buffer, sizeof(buffer));
	if (n < 0) {
		fprintf(stderr, "Could not send feature report (%ls)\n", hid_error(hdev));
		return -1;
	}

	return 0;
}

int rnt_poll_result(rnt_hdl_t hdl, unsigned char *cmd, int cmd_maxlen)
{
	hid_device *hdev = hdl->hdev;
	unsigned char buffer[hdl->report_size+1];
	int res_len;
	int n;

	if (!hdev) {
		return -1;
	}

	memset(buffer, 0, sizeof(buffer));
	buffer[0] = 0x00; // report ID set to 0 (device has only one)

	n = hid_get_feature_report(hdev, buffer, sizeof(buffer));
	if (n < 0) {
		fprintf(stderr, "Could not send feature report (%ls)\n", hid_error(hdev));
		return -1;
	}
	if (n==0) {
		return 0;
	}
	res_len = n-1;

	if (res_len>0) {
		int copy_len;

		copy_len = res_len;
		if (copy_len > cmd_maxlen) {
			copy_len = cmd_maxlen;
		}
		if (cmd) {
			memcpy(cmd, buffer+1, copy_len);
		}
	}

	return res_len;
}

int rnt_exchange(rnt_hdl_t hdl, unsigned char *outcmd, int outlen, unsigned char *result, int result_max)
{
	int n;

	n = rnt_send_cmd(hdl, outcmd, outlen);
	if (n<0) {
		// only complain when this fails on non-legacy devices
		if (hdl->hdev)
			fprintf(stderr, "Error sending command\n");
		return -1;
	}

	/* Answer to the command comes later. For now, this is polled, but in
	 * the future an interrupt-in transfer could be used. */
	do {
		n = rnt_poll_result(hdl, result, result_max);
		if (n < 0) {
			fprintf(stderr, "Error\r\n");
			break;
		}
		if (n==0) {
//			printf("."); fflush(stdout);
		}

	} while (n==0);

	return n;
}

int rnt_suspendPolling(rnt_hdl_t hdl, unsigned char suspend)
{
	unsigned char cmd[2];
	int n;

	if (!hdl) {
		return -1;
	}

	cmd[0] = RQ_GCN64_SUSPEND_POLLING;
	cmd[1] = suspend;

	n = rnt_exchange(hdl, cmd, 2, cmd, sizeof(cmd));
	if (n<0)
		return n;

	return 0;
}

int rnt_setConfig(rnt_hdl_t hdl, unsigned char param, unsigned char *data, unsigned char len)
{
	unsigned char cmd[2 + len];
	int n;

	if (!hdl) {
		return -1;
	}

	cmd[0] = RQ_GCN64_SET_CONFIG_PARAM;
	cmd[1] = param;
	memcpy(cmd + 2, data, len);

	n = rnt_exchange(hdl, cmd, 2 + len, cmd, sizeof(cmd));
	if (n<0)
		return n;

	return 0;
}

int rnt_getConfig(rnt_hdl_t hdl, unsigned char param, unsigned char *rx, unsigned char rx_max)
{
	unsigned char cmd[2];
	int n;

	if (!hdl) {
		return -1;
	}

	cmd[0] = RQ_GCN64_GET_CONFIG_PARAM;
	cmd[1] = param;

	n = rnt_exchange(hdl, cmd, 2, rx, rx_max);
	if (n<2)
		return n;

	n -= 2;

	// Drop the leading CMD and PARAM
	if (n) {
		memmove(rx, rx+2, n);
	}

	return n;
}

int rnt_getVersion(rnt_hdl_t hdl, char *dst, int dstmax)
{
	unsigned char cmd[32];
	int n;

	if (!hdl) {
		return -1;
	}

	if (dstmax <= 0)
		return -1;

	/* legacy device. Version must be built from */
	if (!hdl->hdev) {
		snprintf(dst, dstmax, "%d.%d(.x)", hdl->version_major, hdl->version_minor);
		return 0;
	}

	cmd[0] = RQ_GCN64_GET_VERSION;

	n = rnt_exchange(hdl, cmd, 1, cmd, sizeof(cmd));
	if (n<0)
		return n;

	dst[0] = 0;
	if (n > 1) {
		strncpy(dst, (char*)cmd+1, n);
	}
	dst[dstmax-1] = 0;

	return 0;
}

int rnt_getSignature(rnt_hdl_t hdl, char *dst, int dstmax)
{
	unsigned char cmd[40];
	int n;

	if (!hdl) {
		return -1;
	}

	if (dstmax <= 0)
		return -1;

	cmd[0] = RQ_GCN64_GET_SIGNATURE;

	n = rnt_exchange(hdl, cmd, 1, cmd, sizeof(cmd));
	if (n<0)
		return n;

	dst[0] = 0;
	if (n > 1) {
		strncpy(dst, (char*)cmd+1, n);
	}
	dst[dstmax-1] = 0;

	return 0;
}

int rnt_forceVibration(rnt_hdl_t hdl, unsigned char channel, unsigned char vibrate)
{
	unsigned char cmd[3];
	int n;

	if (!hdl) {
		return -1;
	}

	cmd[0] = RQ_GCN64_SET_VIBRATION;
	cmd[1] = channel;
	cmd[2] = vibrate;

	n = rnt_exchange(hdl, cmd, 3, cmd, sizeof(cmd));
	if (n<0)
		return n;

	return 0;
}

int rnt_getControllerType(rnt_hdl_t hdl, int chn)
{
	unsigned char cmd[32];
	int n;

	if (!hdl) {
		return -1;
	}

	cmd[0] = RQ_GCN64_GET_CONTROLLER_TYPE;
	cmd[1] = chn;

	n = rnt_exchange(hdl, cmd, 2, cmd, sizeof(cmd));
	if (n<0)
		return n;
	if (n<3)
		return -1;

	return cmd[2];
}

const char *rnt_controllerName(int type)
{
	/* Defines from requests.h */
	switch(type) {
		case CTL_TYPE_NONE: return "No controller";
		case CTL_TYPE_N64_NEW:
		case CTL_TYPE_N64: return "N64 Controller";
		case CTL_TYPE_GAMECUBE_NEW:
		case CTL_TYPE_GC: return "GC Controller";
		case CTL_TYPE_GCKB: return "GC Keyboard";
		case CTL_TYPE_CLASSIC: return "Classic controller";
		case CTL_TYPE_SNES: return "SNES controller";
		case CTL_TYPE_NES: return "NES controller";
		case CTL_TYPE_MD: return "Megadrive controller";
		case CTL_TYPE_SMS: return "SMS controller";
		case CTL_TYPE_PCE: return "PC engine controller";
		case CTL_TYPE_PCE6: return "PC engine 6 button controller";
		default:
			return "Unknown";
	}
}

int rnt_bootloader(rnt_hdl_t hdl)
{
	unsigned char cmd[4];
	int cmdlen;

	if (!hdl) {
		return -1;
	}

	cmd[0] = RQ_GCN64_JUMP_TO_BOOTLOADER;
	cmdlen = 1;

	rnt_exchange(hdl, cmd, cmdlen, cmd, sizeof(cmd));

	return 0;
}


