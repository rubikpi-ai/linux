
/** @file  event.c
 *
 * @brief This file handles event for 11mc/11az Wifi location services
 * application
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

#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <linux/wireless.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/ethernet.h>
#include "../mlanutl/mlanutl.h"
#include "mlanwls.h"
#include "wls_api.h"
#include "wls_param_defines.h"
#include "range_kalman.h"

#define HostCmd_CMD_DEBUG (0x8B)

#define CUS_EVT_TOD_TOA "EVENT=TOD-TOA"

#define CUS_EVT_MLAN_CSI "EVENT=MLAN_CSI"
#define CSI_DUMP_FILE_MAX 1200000 // 1.2MB

/** CSI record data structure */
typedef struct _csi_record_ds {
	/** Length in DWORDS, including header */
	t_u16 Len;
	/** CSI signature. 0xABCD fixed */
	t_u16 CSI_Sign;
	/** User defined HeaderID  */
	t_u32 CSI_HeaderID;
	/** Packet info field */
	t_u16 PKT_info;
	/** Frame control field for the received packet*/
	t_u16 FCF;
	/** Timestamp when packet received */
	t_u64 TSF;
	/** Received Packet Destination MAC Address */
	t_u8 Dst_MAC[6];
	/** Received Packet Source MAC Address */
	t_u8 Src_MAC[6];
	/** RSSI for antenna A */
	t_u8 Rx_RSSI_A;
	/** RSSI for antenna B */
	t_u8 Rx_RSSI_B;
	/** Noise floor for antenna A */
	t_u8 Rx_NF_A;
	/** Noise floor for antenna A */
	t_u8 Rx_NF_B;
	/** Rx signal strength above noise floor */
	t_u8 Rx_SINR;
	/** Channel */
	t_u8 channel;
	/** user defined Chip ID */
	t_u16 chip_id;
	/** Reserved */
	t_u32 rsvd;
	/** CSI data length in DWORDs */
	t_u32 CSI_Data_Length;
	/** Start of CSI data */
	t_u8 CSI_Data[0];
	/** At the end of CSI raw data, user defined TailID of 4 bytes*/
} __ATTRIB_PACK__ csi_record_ds;

/** Event body : MLAN_CSI */
typedef struct _event_mlan_csi {
	/** Event string: EVENT=MLAN_CSI */
	char event_str[14];
	/** Event sequence # */
	t_u16 sequence;
	csi_record_ds csi_record;
} __ATTRIB_PACK__ event_mlan_csi;

typedef struct _csi_ack_cmd {
	/** HostCmd_DS_GEN */
	HostCmd_DS_GEN cmd_hdr;
	/** Command Body */
	t_u16 action;
	t_u16 sub_id;
	t_u32 ack;
	t_u32 phase_roll;
	t_u32 firstpath_delay;
	t_u32 fft_size_pointer;
	t_u32 csi_tsf;
} __ATTRIB_PACK__ hostcmd_csi_ack;

extern wls_app_data_t gwls_data;
extern int mlanwls_prepare_buffer(t_u8 *buffer, char *cmd, t_u32 num,
				  char *args[]);

#define RANGE_DRIVE_VAR 4e-5f // in meter/(s^2)
#define RANGE_MEASUREMENT_VAR 4e-2f // in meter^2
#define RANGE_RATE_INIT 1e-3f // in (meter/s)^2

range_kalman_state range_input_str = {0};

/**
 *  @brief Writes the Distance value to a file /tmp/mwu.log to display in
 * GuiTracker app
 *
 *  @param distance distance value obtained from FTM complete event
 *
 *  @return  error
 */
static int mlanwls_update_distance_to_gui(int distance, unsigned int tsf)
{
	FILE *logfile;
	char buf[50];
	int distance_m, distance_cm;
	unsigned int time_ms = tsf / 1000;
	float distance_flt = 1.0f * distance / (1 << 8); // in meters
	float distance_kalman;

	if (range_input_str.time == 0) {
		range_kalman_init(&range_input_str, distance_flt, time_ms,
				  RANGE_DRIVE_VAR, RANGE_MEASUREMENT_VAR,
				  RANGE_RATE_INIT);
		range_input_str.time = 1;
	} else {
		range_input_str.range_measurement = distance_flt;
		range_input_str.time = time_ms;
		range_kalman(&range_input_str);
	}
	distance_kalman = range_input_str.last_range;
	printf("Measured Distance: %f m; Kalman Distance: %f m [%d ms]\n",
	       distance_flt, distance_kalman, time_ms);

	logfile = fopen("/var/www/html/mwu.log", "w");
	if (logfile == NULL) {
		printf("Could not open log file\n");
		return 2;
	}
	distance_m = distance >> 8;
	distance_cm = ((distance & 0xff) * 100) >> 8;

	sprintf(buf, "%d.%02d\n", distance_m, distance_cm);
	fwrite(buf, 1, strlen(buf), logfile);
	fclose(logfile);

	return 0;
}

static void proc_csi_event(event_header *event, unsigned int *resArray)
{
	unsigned char *rdPtr =
		(unsigned char *)&(((event_mlan_csi *)event)->csi_record);
	unsigned int headerBuffer;
	unsigned int *csiBuffer;
	unsigned int *fftInBuffer;
	hal_wls_packet_params_t packetparams;
	unsigned short csi_len;
	unsigned int tsf;
	int distance;

	memcpy(&headerBuffer, rdPtr, sizeof(unsigned int));

	csi_len = headerBuffer & 0x1fff; // 13 LSBs
	csiBuffer = (unsigned int *)malloc(sizeof(unsigned int) * csi_len);

	memcpy(csiBuffer, rdPtr, sizeof(unsigned int) * csi_len);

	distance = csiBuffer[19];
	tsf = csiBuffer[20];

	if (distance >= 0)
		mlanwls_update_distance_to_gui(distance, tsf);

	fftInBuffer = (unsigned int *)malloc(sizeof(unsigned int) *
					     (MAX_RX * MAX_TX + NUM_PROC_BUF) *
					     MAX_IFFT_SIZE_CSI);

	packetparams.chNum = gwls_data.channel;

	resArray[0] = 0xffffffff;
	resArray[1] = 0xffffffff;
	resArray[2] = 0xffffffff;
	resArray[3] = 0xffffffff;

	wls_process_csi(csiBuffer, fftInBuffer, &packetparams,
			&gwls_data.wls_processing_input, resArray);

	// record TSF
	resArray[3] = csiBuffer[3];

	printf("EVENT: MLAN_CSI Processing results: %d | %d (%x|%x), TSF %x \n",
	       resArray[0], resArray[1], resArray[2],
	       *((unsigned int *)&gwls_data.wls_processing_input), resArray[3]);

	free(fftInBuffer);
}

#ifdef PRINT_CSI_TO_FILE
static void print_csi_event(event_header *event, t_u16 size, char *if_name)
{
	struct stat fbuf;
	int fstat;
	FILE *csi_fp = NULL;
	char filename[64];
	/* Mask out Bit15,14,13 for RAW CSI */
	t_u16 csi_length = 0x1fff & ((event_mlan_csi *)event)->csi_record.Len;
	t_u16 seq = ((event_mlan_csi *)event)->sequence;
	static t_u16 last_seq = 0;
	static t_u16 total_lost_count = 0;

	/* Print CSI EVENT data */
	printf("EVENT: MLAN_CSI\n");
	printf("Sequence: %d\n", ((event_mlan_csi *)event)->sequence);
	printf("Length: %d DWORDs\n", csi_length);
	// printf("CSI record:\n");
	// hexdump(NULL, (void *)&(((event_mlan_csi *)event)->csi_record),
	//     4*csi_length, ' ');

	/* Save CSI data to file */

	snprintf(filename, sizeof(filename), "csidump_%s.txt", if_name);
	fstat = stat(filename, &fbuf);

	if ((fstat == 0) && (fbuf.st_size >= CSI_DUMP_FILE_MAX)) {
		printf("File %s reached maximum size. Not saving CSI records.\n",
		       filename);
		printf("ERR: Lost csi event as %s reached maximum size for %d times\n",
		       filename, seq - last_seq);
	} else {
		t_u16 fpos;
		csi_fp = fopen(filename, "a+");

		if (seq == 0 || last_seq == 0 || seq == (last_seq + 1))
			last_seq = seq;
		else {
			total_lost_count += seq - last_seq;
			printf("ERR: Lost csi event for %d times, total_lost_count = %d\n",
			       seq - last_seq, total_lost_count);
			last_seq = seq;
		}

		if (csi_fp) {
			for (fpos = 0; fpos < csi_length; fpos++) {
				fprintf(csi_fp, "%08x ",
					*((t_u32 *)&(((event_mlan_csi *)event)
							     ->csi_record) +
					  fpos));
			}
			fclose(csi_fp);
		}
		chmod(filename, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP |
					S_IROTH | S_IWOTH);
	}
}
#endif

/**
 *  @brief Send hostcmd IOCTL to driver
 *  @param cmd_buf  pointer to Host Command buffer
 *
 *  @return     MLAN_STATUS_SUCCESS--success, otherwise--fail
 */

static int mlanwls_send_ioctl(t_u8 *cmd_buf)
{
	struct ifreq ifr;
	struct eth_priv_cmd *cmd = NULL;
	int ret = MLAN_STATUS_SUCCESS;

	/*hexdump((void *) cmd_buf, MRVDRV_SIZE_OF_CMD_BUFFER, ' ');*/

	if (!cmd_buf) {
		printf("ERR:IOCTL Failed due to null cmd buffer!\n");
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	cmd = (struct eth_priv_cmd *)malloc(sizeof(struct eth_priv_cmd));
	if (!cmd) {
		printf("ERR:Cannot allocate buffer for command!\n");
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	/* Fill up buffer */
#ifdef USERSPACE_32BIT_OVER_KERNEL_64BIT
	memset(cmd, 0, sizeof(struct eth_priv_cmd));
	memcpy(&cmd->buf, cmd_buf, sizeof(cmd_buf));
#else
	cmd->buf = cmd_buf;
#endif
	cmd->used_len = 0;
	cmd->total_len = MRVDRV_SIZE_OF_CMD_BUFFER;

	/* Perform IOCTL */
	memset(&ifr, 0, sizeof(struct ifreq));
	strncpy(ifr.ifr_ifrn.ifrn_name, dev_name, strlen(dev_name));
	ifr.ifr_ifru.ifru_data = (void *)cmd;

	if (ioctl(sockfd, MLAN_ETH_PRIV, &ifr)) {
		perror("mlanwls");
		fprintf(stderr, "IOCTL fail\n");
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

done:
	if (cmd)
		free(cmd);
	return ret;
}

static int mlanwls_csi_ack_resp(char *cmd_name, t_u8 *buf)
{
	t_u32 hostcmd_size = 0;
	HostCmd_DS_GEN *hostcmd = NULL;
	int ret = MLAN_STATUS_SUCCESS;
	hostcmd_csi_ack *phostcmd = NULL;

	buf += strlen(CMD_NXP) + strlen(cmd_name);
	memcpy((t_u8 *)&hostcmd_size, buf, sizeof(t_u32));
	buf += sizeof(t_u32);

	hostcmd = (HostCmd_DS_GEN *)buf;
	hostcmd->command = le16_to_cpu(hostcmd->command);
	hostcmd->size = le16_to_cpu(hostcmd->size);

	hostcmd->command &= ~HostCmd_RET_BIT;

	switch (hostcmd->command) {
	case HostCmd_CMD_DEBUG:
		phostcmd = (hostcmd_csi_ack *)buf;
		if (!le16_to_cpu(phostcmd->cmd_hdr.result)) {
			printf("SUCCESS\n");
		} else {
			printf("FAIL\n");
		}
		break;

	default:
		break;
	}

	return ret;
}

static void send_csi_ack(unsigned int *resArray)
{
	t_u8 *buffer = NULL;
	hostcmd_csi_ack *phostcmd = NULL;

	/* Initialize buffer */
	buffer = (t_u8 *)malloc(BUFFER_LENGTH);
	if (!buffer) {
		printf("ERR:Cannot allocate buffer for command!\n");
		return;
	}

	mlanwls_prepare_buffer(buffer, "hostcmd", 0, NULL);
	phostcmd = (hostcmd_csi_ack *)(buffer +
				       (strlen(CMD_NXP) + strlen("hostcmd") +
					sizeof(t_u32)));

	/*Parse the arguments*/
	phostcmd->cmd_hdr.command = cpu_to_le16(HostCmd_CMD_DEBUG);
	phostcmd->cmd_hdr.size = S_DS_GEN + sizeof(hostcmd_csi_ack);
	phostcmd->action = 0;
	phostcmd->sub_id = 0x333;
	phostcmd->ack = 1;

	phostcmd->phase_roll = resArray[0];
	phostcmd->firstpath_delay = resArray[1];
	phostcmd->fft_size_pointer = resArray[2];
	phostcmd->csi_tsf = resArray[3];

	phostcmd->cmd_hdr.size += 6 * sizeof(t_u32);
	phostcmd->cmd_hdr.size = cpu_to_le16(phostcmd->cmd_hdr.size);

	/* send command */
	mlanwls_send_ioctl(buffer);

	/* handle command response */
	mlanwls_csi_ack_resp("hostcmd", buffer);
}

/**
 *  @brief              This function parses for NETLINK events
 *
 *  @param nlh          Pointer to Netlink message header
 *  @param bytes_read   Number of bytes to be read
 *  @param evt_conn     A pointer to a output buffer. It sets TRUE when it gets
 *  					the event CUS_EVT_OBSS_SCAN_PARAM, otherwise
 * FALSE
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static int drv_nlevt_handler(struct nlmsghdr *nlh, int bytes_read,
			     int *evt_conn)
{
	int len, plen;
	t_u8 *buffer = NULL;
	t_u32 event_id = 0;
	event_header *event = NULL;
	char if_name[IFNAMSIZ + 1];
	unsigned int csi_res_array[8];

	/* Initialize receive buffer */
	buffer = (t_u8 *)malloc(NL_MAX_PAYLOAD);
	if (!buffer) {
		printf("ERR: Could not alloc buffer\n");
		return MLAN_STATUS_FAILURE;
	}
	memset(buffer, 0, NL_MAX_PAYLOAD);

	*evt_conn = FALSE;
	while ((unsigned int)bytes_read >= NLMSG_HDRLEN) {
		len = nlh->nlmsg_len; /* Length of message including header */
		plen = len - NLMSG_HDRLEN;
		if (len > bytes_read || plen < 0) {
			free(buffer);
			/* malformed netlink message */
			return MLAN_STATUS_FAILURE;
		}
		if ((unsigned int)len > NLMSG_SPACE(NL_MAX_PAYLOAD)) {
			printf("ERR:Buffer overflow!\n");
			free(buffer);
			return MLAN_STATUS_FAILURE;
		}
		memset(buffer, 0, NL_MAX_PAYLOAD);
		memcpy(buffer, NLMSG_DATA(nlh), plen);

		if (NLMSG_OK(nlh, len)) {
			memcpy(&event_id, buffer, sizeof(event_id));

			if (((event_id & 0xFF000000) == 0x80000000) ||
			    ((event_id & 0xFF000000) == 0)) {
				event = (event_header *)buffer;
			} else {
				memset(if_name, 0, IFNAMSIZ + 1);
				memcpy(if_name, buffer, IFNAMSIZ);
				event = (event_header *)(buffer + IFNAMSIZ);
			}
		}

		/*Prints the events*/
		if (event) {
			switch (event->event_id) {
			case EVENT_WLS_GENERIC:
				process_wls_generic_event((t_u8 *)event,
							  bytes_read, if_name);
				break;
			default:
				// hexdump(NULL, (void *) event, bytes_read, '
				// ');

				if (!strncmp((char *)event, CUS_EVT_MLAN_CSI,
					     strlen(CUS_EVT_MLAN_CSI))) {
					/* procss CSI data */
					proc_csi_event(event, csi_res_array);
					send_csi_ack(csi_res_array);
#ifdef PRINT_CSI_TO_FILE
					print_csi_event(event, bytes_read,
							if_name);
#endif
				}

				// printf("Dumping event buffer:\n");

				break;
			}
		}
		len = NLMSG_ALIGN(len);
		bytes_read -= len;
		nlh = (struct nlmsghdr *)((char *)nlh + len);
	}
	free(buffer);
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief Configure and read event data from netlink socket
 *
 *  @param nl_sk        Netlink socket handler
 *  @param msg          Pointer to message header
 *  @param ptv          Pointer to struct timeval
 *
 *  @return             Number of bytes read or MLAN_STATUS_FAILURE
 */
static int read_event(int nl_sk, struct msghdr *msg, struct timeval *ptv)
{
	int count = -1;
	int ret = MLAN_STATUS_FAILURE;
	fd_set rfds;

	/* Setup read fds and initialize event buffers */
	FD_ZERO(&rfds);
	FD_SET(nl_sk, &rfds);

	/* Wait for reply */
	ret = select(nl_sk + 1, &rfds, NULL, NULL, ptv);

	if (ret == MLAN_STATUS_FAILURE) {
		/* Error */
		ptv->tv_sec = DEFAULT_SCAN_INTERVAL;
		ptv->tv_usec = 0;
		goto done;
	}
	if (!FD_ISSET(nl_sk, &rfds)) {
		/* Unexpected error. Try again */
		ptv->tv_sec = DEFAULT_SCAN_INTERVAL;
		ptv->tv_usec = 0;
		goto done;
	}
	/* Success */
	count = recvmsg(nl_sk, msg, 0);

done:
	return count;
}

/**
 *  @brief Run the application
 *
 *  @param nl_sk    Netlink socket
 *
 *  @return         N/A
 */
void mlanwls_event_monitor(int nl_sk)
{
	struct timeval tv;
	int bytes_read, evt_conn;
	struct msghdr msg;
	struct sockaddr_nl dest_addr;
	struct nlmsghdr *nlh;
	struct iovec iov;

	/* Initialize timeout value */
	tv.tv_sec = DEFAULT_SCAN_INTERVAL;
	tv.tv_usec = 0;

	/* Initialize netlink header */
	nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(NL_MAX_PAYLOAD));
	if (!nlh) {
		printf("[ERROR] Could not allocate space for netlink header\n");
		goto done;
	}
	memset(nlh, 0, NLMSG_SPACE(NL_MAX_PAYLOAD));
	/* Fill the netlink message header */
	nlh->nlmsg_len = NLMSG_SPACE(NL_MAX_PAYLOAD);
	nlh->nlmsg_pid = getpid(); /* self pid */
	nlh->nlmsg_flags = 0;

	/* Initialize I/O vector */
	memset(&iov, 0, sizeof(struct iovec));
	iov.iov_base = (void *)nlh;
	iov.iov_len = nlh->nlmsg_len;

	/* Set destination address */
	memset(&dest_addr, 0, sizeof(struct sockaddr_nl));
	dest_addr.nl_family = AF_NETLINK;
	dest_addr.nl_pid = 0; /* Kernel */
	dest_addr.nl_groups = NL_MULTICAST_GROUP;

	/* Initialize message header */
	memset(&msg, 0, sizeof(struct msghdr));
	msg.msg_name = (void *)&dest_addr;
	msg.msg_namelen = sizeof(dest_addr);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	while (!gwls_data.terminate_app) {
		/* event buffer is received for all the interfaces */
		bytes_read = read_event(nl_sk, &msg, &tv);
		/* handle only NETLINK events here */
		drv_nlevt_handler((struct nlmsghdr *)nlh, bytes_read,
				  &evt_conn);
	}

done:
	if (nl_sk > 0)
		close(nl_sk);
	if (nlh)
		free(nlh);
	return;
}
