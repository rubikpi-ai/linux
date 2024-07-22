
/** @file  wls_subspace_processing.h
 *
 * @brief This file contains header file for sub-space based fine timing
 * calculation
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
 * Header file for sub-space based fine timing calculation
 ************************************************************************/

#ifndef WLS_SUBSPACE_PROCESSING
#define WLS_SUBSPACE_PROCESSING

#include "wls_structure_defs.h"

int calcSubspaceFineTiming(
	hal_pktinfo_t *pktinfo, // structure with CSI buffer parameters
	unsigned int *fftOutBuffer, // buffer holding time-domain CSI
	unsigned int *totalpower, // array holding power per rx/tx channel
	int firstPathDelay, // existing first path estimate
	int *fineTimingRes, // result of algorithm
	unsigned int *procBuffer, // buffer for processing - needs about 2k
				  // bytes
	hal_wls_packet_params_t *packetparams // passing packetinfo to
					      // determin 2.4/5G
);

#endif
