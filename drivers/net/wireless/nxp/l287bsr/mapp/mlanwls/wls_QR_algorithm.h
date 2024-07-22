
/** @file wls_QR_algorithm.h
 *
 * @brief This file contains header for QR math functions
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
 * DFW header for QR math functions
 ************************************************************************/

#ifndef WLS_QR_ALGORITHM_H
#define WLS_QR_ALGORITHM_H

// MAX_MAT_SIZE needs to be >=2*SIG_SUBSP_DIM_MAX
#define MAX_MAT_SIZE 32

#ifdef ARM_DS5
#define SQRTF(x) __sqrt(x)
#define FABSF(x) __fabsf(x)
#else
#define SQRTF(x) sqrtf(x)
#define FABSF(x) fabsf(x)
#endif

// eigen(Shur) decomposition for symmetric matrix, returns eigen vectors in Q
int QR_algorithm(float *inMatArr, float *resD, int matSizeN, int low_accuracy);

// eigen(Shur) decomposition for unsymmetric matrix, no Q
int unsym_QR_algorithm(float *inMatArr, float *resD, int matSizeN);

// solves LS using QR
void QR_decomposition(float *inMatArr, float *resD, int matSizeN, int matSizeM);

void myBackSub(float *Q_MATR, float *R_MATR, float *MAT_OUT, int matSizeN,
	       int matSizeM);

#endif
