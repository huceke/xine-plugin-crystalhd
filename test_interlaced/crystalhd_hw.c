/*
 * Copyright (C) 2009 Edgar Hucek
 *
 * This file is part of xine, a free video player.
 *
 * xine is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * xine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * crystalhd_hw.c: H264 Video Decoder utilizing Broadcom Crystal HD engine
 */

#include "crystalhd.h"

const char* g_DtsStatusText[] = {
        "BC_STS_SUCCESS",
        "BC_STS_INV_ARG",
        "BC_STS_BUSY",
        "BC_STS_NOT_IMPL",
        "BC_STS_PGM_QUIT",
        "BC_STS_NO_ACCESS",
        "BC_STS_INSUFF_RES",
        "BC_STS_IO_ERROR",
        "BC_STS_NO_DATA",
        "BC_STS_VER_MISMATCH",
        "BC_STS_TIMEOUT",
        "BC_STS_FW_CMD_ERR",
        "BC_STS_DEC_NOT_OPEN",
        "BC_STS_ERR_USAGE",
        "BC_STS_IO_USER_ABORT",
        "BC_STS_IO_XFR_ERROR",
        "BC_STS_DEC_NOT_STARTED",
        "BC_STS_FWHEX_NOT_FOUND",
        "BC_STS_FMT_CHANGE",
        "BC_STS_HIF_ACCESS",
        "BC_STS_CMD_CANCELLED",
        "BC_STS_FW_AUTH_FAILED",
        "BC_STS_BOOTLOADER_FAILED",
        "BC_STS_CERT_VERIFY_ERROR",
        "BC_STS_DEC_EXIST_OPEN",
        "BC_STS_PENDING",
        "BC_STS_CLK_NOCHG"
};

HANDLE crystalhd_open () {

	BC_STATUS res;
	uint32_t mode = DTS_PLAYBACK_MODE | DTS_LOAD_FILE_PLAY_FW | DTS_PLAYBACK_DROP_RPT_MODE | DTS_DFLT_RESOLUTION(vdecRESOLUTION_CUSTOM) /*| DTS_SINGLE_THREADED_MODE */;
  HANDLE hDevice = NULL;

	res = DtsDeviceOpen(&hDevice, mode);
	if (res != BC_STS_SUCCESS) {
		printf("crystalhd_h264: ERROR: Failed to open Broadcom Crystal HD\n");
		return 0;
	}

	printf("crystalhd_h264: open device done\n");

  return hDevice;

}

/*
HANDLE crystalhd_close(xine_t *xine, HANDLE hDevice) {

	BC_STATUS res;

	if(hDevice)  {
		res = DtsDeviceClose(hDevice);
	  if (res != BC_STS_SUCCESS) {
  		xprintf(xine, XINE_VERBOSITY_LOG,"crystalhd: ERROR: DtsDeviceClose\n");
  	}
	}

	xprintf(xine, XINE_VERBOSITY_LOG,"crystalhd: clsoe open done\n");

  return 0;
}
*/

HANDLE crystalhd_stop (xine_t *xine, HANDLE hDevice) {

	BC_STATUS res;

  if(hDevice) {

		res = DtsFlushRxCapture(hDevice, TRUE);
	  if (res != BC_STS_SUCCESS) {
  		xprintf(xine, XINE_VERBOSITY_LOG,"crystalhd: ERROR: DtsFlushRxCapture\n");
  	}

		res = DtsStopDecoder(hDevice);
	  if (res != BC_STS_SUCCESS) {
  		xprintf(xine, XINE_VERBOSITY_LOG,"crystalhd: ERROR: DtsStopDecoder\n");
  	}

		res = DtsCloseDecoder(hDevice);
	  if (res != BC_STS_SUCCESS) {
  		xprintf(xine, XINE_VERBOSITY_LOG,"crystalhd: ERROR: DtsCloseDecoder\n");
  	}

  }

	xprintf(xine, XINE_VERBOSITY_LOG,"crystalhd: stop device done\n");

  return hDevice;
}

HANDLE crystalhd_start (xine_t *xine, HANDLE hDevice, BCM_STREAM_TYPE stream_type, BCM_VIDEO_ALGO algo) {

	BC_STATUS res;

  if(hDevice) {

	hDevice = crystalhd_stop (xine, hDevice);
	/* Init BCM70010 */
  	res = DtsOpenDecoder(hDevice, stream_type);
  	if (res != BC_STS_SUCCESS) {
  		xprintf(xine, XINE_VERBOSITY_LOG,"crystalhd: Failed to open decoder\n");
		return hDevice;
  	}

  	res = DtsSetVideoParams(hDevice, algo, FALSE, FALSE, FALSE, 0 /* | 0x80 */);
  	if (res != BC_STS_SUCCESS) {
  		xprintf(xine, XINE_VERBOSITY_LOG,"crystalhd: Failed to set video params\n");
  	}

  	res = DtsSet422Mode(hDevice, MODE422_YUY2);
  	if (res != BC_STS_SUCCESS) {
  		xprintf(xine, XINE_VERBOSITY_LOG,"crystalhd: Failed to set 422 mode\n");
  	}

    /*
  	res = DtsSetColorSpace(hDevice, OUTPUT_MODE422_YUY2);
  	if (res != BC_STS_SUCCESS) {
  		xprintf(xine, XINE_VERBOSITY_LOG,"crystalhd: Failed to set 422 mode\n");
  	}
    */

  	res = DtsStartDecoder(hDevice);
  	if (res != BC_STS_SUCCESS) {
  		xprintf(xine, XINE_VERBOSITY_LOG,"crystalhd: Failed to start decoder\n");
  	}

  	res = DtsStartCapture(hDevice);
  	if (res != BC_STS_SUCCESS) {
  		xprintf(xine, XINE_VERBOSITY_LOG,"crystalhd: Failed to start capture\n");
  	}

  }

	xprintf(xine, XINE_VERBOSITY_LOG,"crystalhd: start device done\n");

  return hDevice;
}

