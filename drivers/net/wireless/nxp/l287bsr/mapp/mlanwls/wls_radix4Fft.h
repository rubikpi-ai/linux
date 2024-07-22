
/** @file wls_radix4Fft.h
 *
 * @brief This file contains header file for fixed-point FFT functions
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
 * DFW header file for fixed-point FFT functions
 ************************************************************************/

#ifndef WLS_RADIX4_FFT
#define WLS_RADIX4_FFT

#include "wls_param_defines.h"

#define INT8 signed char
#define INT16 short
#define INT32 int
#ifdef STA_20_ONLY
#define MAX_FFT_SIZE_256
#define MAX_FFT_SIZE 256
#else
#define MAX_FFT_SIZE_2048
#define MAX_FFT_SIZE 2048
#endif
//#define TWIDDLE_HALF_SIZE
#define TWIDDLE_BIPT 15
//#define BIASED_ROUNDING

// call separate stages
void radix4FftStageOne(INT16 *pSrc, INT16 *pDst, int Nfft);
void radix4FftMainStages(INT16 *pSrc, INT16 *pDst, int Nfft,
			 const INT16 *pCoeff, int lenCoeff);
void radix4FftLastStage(INT16 *pSrc, INT16 *pDst, int Nfft, const INT16 *pCoeff,
			int lenCoeff);

void radix4IfftStageOne(INT16 *pSrc, INT16 *pDst, int Nfft);
void radix4IfftMainStages(INT16 *pSrc, INT16 *pDst, int Nfft,
			  const INT16 *pCoeff, int lenCoeff);
void radix4IfftLastStage(INT16 *pSrc, INT16 *pDst, int Nfft,
			 const INT16 *pCoeff, int lenCoeff);

// auxiliary function
unsigned int reverse(register unsigned int x);
#ifndef ARM_DS5
int __clz(int x);
#endif

void radix4Fft(INT16 *pSrc, INT16 *pDst, int Nfft, const INT16 *pCoeff,
	       int lenCoeff);
void radix4Fft4in64(unsigned int *loadPtr, unsigned int *fftOutBfr,
		    const INT16 *pCoeff, int lenCoeff);

void radix4Ifft(INT16 *pSrc, INT16 *pDst, int Nfft, const INT16 *pCoeff,
		int lenCoeff);
void radix4IfftParallel(INT16 *pSrc, INT16 *pDst, int Nfft, const INT16 *pCoeff,
			int lenCoeff);
void radix4IfftStride(INT16 *pSrc, INT16 *pDst, int Nfft, const INT16 *pCoeff,
		      int lenCoeff);

void radix2Ifft(INT16 *pSrc, int Nfft, const INT16 *pCoeff, int lenCoeff);
void radix2IfftParallel(INT16 *pSrc, int Nfft, const INT16 *pCoeff,
			int lenCoeff);
void radix2IfftStride(INT16 *pSrc, int Nfft, const INT16 *pCoeff, int lenCoeff);

void radix2FftFlt(float *pBfr, int Nfft, const float *pCoeff, int lenCoeff);
void radix2IfftFlt(float *pBfr, int Nfft, const float *pCoeff, int lenCoeff);

#define MAX_FFT_FLT 64
extern const float twiddleTableFlt[2 * MAX_FFT_FLT];

#ifdef TWIDDLE_HALF_SIZE
extern const INT16 radix4FftTwiddleArr[MAX_FFT_SIZE];
#else
extern const INT16 radix4FftTwiddleArr[2 * MAX_FFT_SIZE];
#endif

#endif
