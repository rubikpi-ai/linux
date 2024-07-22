
/** @file wls_processing.h
 *
 * @brief This file contains header file for CSI processing functions
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
 * DFW header file for CSI processing functions
 ************************************************************************/

#ifdef DFW_CSI_PROC
#include "dsp_cmd.h"
#endif
#include "wls_structure_defs.h"

#ifndef WLS_PROCESSING_H
#define WLS_PROCESSING_H

int myAtan2(int valI, int valQ);

int myAsin(int x);

unsigned int mySqrt(int x);

unsigned int mySqrtLut(int x);

void readHexDataDemodulateProcess(hal_pktinfo_t *pktinfo,
				  hal_wls_processing_input_params_t *inputVals,
				  unsigned int *dataPtr, int csiDataSize,
				  unsigned int *fftInBuffer,
				  unsigned int *powerPerSubband,
				  int *phaseRollPtr, int chNum);
void readHexDataDemodulateProcessParallel(
	hal_pktinfo_t *pktinfo, hal_wls_processing_input_params_t *inputVals,
	unsigned int *dataPtr, int csiDataSize, unsigned int *fftInBfr,
	unsigned int *powerPerSubband, int *phaseRollPtr, int chNum);

void readHexDataDemodulateProcessVhtHeNg1(
	hal_pktinfo_t *pktinfo, hal_wls_processing_input_params_t *inputVals,
	unsigned int *dataPtr, int csiDataSize, unsigned int *fftInBuffer,
	unsigned int *powerPerSubband, int *phaseRollPtr, int chNum);
void readHexDataDemodulateProcessVhtHeNg1Parallel(
	hal_pktinfo_t *pktinfo, hal_wls_processing_input_params_t *inputVals,
	unsigned int *dataPtr, int csiDataSize, unsigned int *fftInBuffer,
	unsigned int *powerPerSubband, int *phaseRollPtr, int chNum);

void detectPhaseJump(hal_pktinfo_t *pktinfo,
		     hal_wls_processing_input_params_t *inputVals,
		     unsigned int *fftInBfr, int *phaseRollPtr);

void calculateTotalPower(hal_pktinfo_t *pktinfo, unsigned int *powerPerSubband,
			 unsigned int *totalpower);

void processLegacyPackets(hal_pktinfo_t *pktinfo, unsigned int *fftInBuffer,
			  int bufferspacing, int *phaseRollPtr);

void interpolatePilots(hal_pktinfo_t *pktinfo, unsigned int *fftInBuffer,
		       int bufferspacing, int *phaseRollPtr,
		       unsigned int *totalpower);
void interpolatePilotsParallel(hal_pktinfo_t *pktinfo,
			       unsigned int *fftInBuffer, int bufferspacing,
			       int *phaseRollPtr, unsigned int *totalpower);

void ifftProcessing(hal_pktinfo_t *pktinfo, unsigned int *fftInBuffer,
		    unsigned int *fftOutBuffer, int bufferspacing);

void interpolateBandEdges20(hal_pktinfo_t *pktinfo, unsigned int *fftInBuffer,
			    int phaseRollNg);
void interpolateBandEdges20Parallel(hal_pktinfo_t *pktinfo,
				    unsigned int *fftInBuffer,
				    int *phaseRollPtr);

void interpolateBandEdges40(hal_pktinfo_t *pktinfo, unsigned int *fftInBuffer,
			    int phaseRollNg);
void interpolateBandEdges40Parallel(hal_pktinfo_t *pktinfo,
				    unsigned int *fftInBuffer,
				    int *phaseRollPtr);

void interpolateBandEdges(hal_pktinfo_t *pktinfo, unsigned int *fftInBuffer,
			  int phaseRollNg);
void interpolateBandEdgesParallel(hal_pktinfo_t *pktinfo,
				  unsigned int *fftInBuffer, int *phaseRollPtr);

void interpolatePairValue(unsigned int *valLeft, unsigned int *valRight,
			  int phaseRollNg);
void interpolatePairValueParallel(unsigned int *valLeft, unsigned int *valRight,
				  int *phaseRollPtr);

void findActiveSubbands(hal_pktinfo_t *pktinfo, unsigned int *powerPerSubband,
			unsigned int *totalpower, int chNum, int ftmSignalBW);

void zeroOutTones(hal_pktinfo_t *pktinfo, unsigned int *fftInBuffer,
		  int bufferspacing);

void removeToneRotation(hal_pktinfo_t *pktinfo, unsigned int *fftInBfr,
			int bufferspacing);
void removeToneRotationParallel(hal_pktinfo_t *pktinfo, unsigned int *fftInBfr,
				int bufferspacing);

void calcPdpAndFirstPathMin(hal_pktinfo_t *pktinfo, unsigned int *fftOutBuffer,
			    unsigned int *pdpOutBuffer,
			    unsigned int *totalpower, int *idxRes,
			    unsigned int *valRes, int *firstPathDelay);

void calcPdpAndMax(hal_pktinfo_t *pktinfo, unsigned int *fftOutBuffer,
		   unsigned int *pdpOutBuffer, unsigned int *totalpower,
		   int *idxRes, unsigned int *valRes);
void calcPdpAndMaxParallel(hal_pktinfo_t *pktinfo, unsigned int *fftOutBuffer,
			   unsigned int *pdpOutBuffer, unsigned int *totalpower,
			   int *idxRes, unsigned int *valRes);

int findFirstPath(hal_pktinfo_t *pktinfo, unsigned int *pdpOutBuffer,
		  int maxIdx, unsigned int maxVal, int stride);

void dumpRawComplex(hal_pktinfo_t *pktinfo, unsigned int *fftBuffer,
		    int peakIdx, unsigned int *destArray);

#endif
