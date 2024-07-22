

/** @file range_kalman.h
 *
 * @brief This file contains Kalman filter for WLS range measurements
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

#ifndef RANGE_KALMAN
#define RANGE_KALMAN

#define RANGE_RUN_FLT
#ifdef RANGE_RUN_FLT
// range is in meters, range_rate in meters/second
// time is in seconds
typedef struct {
	// input
	unsigned long long time;
	float range_measurement;
	// state
	float last_range; // also output <--
	float last_range_rate;
	float R0_11, R0_22, R0_12;
	unsigned long long last_time;
	// model parameters
	float measurement_var;
	float drive_var;
} range_kalman_state;

void range_kalman_init(range_kalman_state *in, float range,
		       unsigned long long time, float range_drive_var,
		       float range_measurement_var, float range_rate_init);
#else
// range format u16.8 in meters
// time format is u64.0 in milliseconds
typedef struct {
	// input
	unsigned long long time;
	unsigned int range_measurement;
	// state
	unsigned short last_range; // also output <--
	signed short last_range_rate;
	unsigned long long last_time;
	unsigned short R0_11, R0_22;
	signed int R0_12;
	// model parameters
	unsigned int measurement_var;
	unsigned int drive_var;
} range_kalman_state;
#endif

int range_kalman(range_kalman_state *in);

#endif
