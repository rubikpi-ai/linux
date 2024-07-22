
/** @file wls_api.h
 *
 * @brief This file contains header file file for WLS processing fixed-point
 * version API
 *
 *  Usage:
 *
 *
 * Copyright 2023 NXP
 *
 * NXP CONFIDENTIAL
 * The source code contained or described herein and all documents related to
 * the source code (Materials) are owned by NXP, its
 * suppliers and/or its licensors. Title to the Materials remains with NXP,
 * its suppliers and/or its licensors. The Materials contain
 * trade secrets and proprietary and confidential information of NXP, its
 * suppliers and/or its licensors. The Materials are protected by worldwide
 * copyright and trade secret laws and treaty provisions. No part of the
 * Materials may be used, copied, reproduced, modified, published, uploaded,
 * posted, transmitted, distributed, or disclosed in any way without NXP's prior
 * express written permission.
 *
 * No license under any patent, copyright, trade secret or other intellectual
 * property right is granted to or conferred upon you by disclosure or delivery
 * of the Materials, either expressly, by implication, inducement, estoppel or
 * otherwise. Any license under such intellectual property rights must be
 * express and approved by NXP in writing.
 *
 */

/************************************************************************
 * Header file for WLS processing fixed-point version API
 ************************************************************************/
#ifndef WLS_API_H
#define WLS_API_H

#include "wls_structure_defs.h"
#ifdef DFW_CSI_PROC
#include "dsp_cmd.h"
#endif

int wls_process_csi(unsigned int *bufferMemory, unsigned int *fftInBuffer,
		    hal_wls_packet_params_t *packetparams,
		    hal_wls_processing_input_params_t *inputVals,
		    unsigned int *resArray);

#endif
