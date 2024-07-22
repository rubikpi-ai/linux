
/** @file  mlanwls.h
 *
 * @brief 11mc/11az Wifi location services application
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
Change log:
     01/24/2022: initial version
************************************************************************/
#ifndef _WLS_H_
#define _WLS_H_

#include "wls_structure_defs.h"

/** Size of command buffer */
#define MRVDRV_SIZE_OF_CMD_BUFFER (3 * 1024)

/** MAC BROADCAST */
#define MAC_BROADCAST 0x1FF
/** MAC MULTICAST */
#define MAC_MULTICAST 0x1FE
/** Default scan interval in second*/
#define DEFAULT_SCAN_INTERVAL 300
/*Max number of peers to support PASN auth simultaneously*/
#define MAX_PASN_PEERS 5

#ifndef NLMSG_HDRLEN
/** NL message header length */
#define NLMSG_HDRLEN ((int)NLMSG_ALIGN(sizeof(struct nlmsghdr)))
#endif

/** Host Command ID : FTM session config and control */
#define HostCmd_CMD_FTM_SESSION_CFG 0x024d
#define HostCmd_CMD_FTM_SESSION_CTRL 0x024E
#define HostCmd_CMD_FTM_FEATURE_CTRL 0x024f
#define HostCmd_CMD_WLS_REQ_FTM_RANGE 0x0250
#define HostCmd_CMD_802_11_ACTION_FRAME 0x00F4
#define HostCmd_CMD_ANQP_ACTION_FRAME 0x0f4
#define HostCmd_CMD_NEIGHBOR_REQ 0x0231

#define ANQP_ELEMENT_QUERY_INFO_ID 0x100
/** AP Geospatial Location */
#define ANQP_ELEMENT_LCI_INFO_ID 0x109
/** AP Civic Location */
#define ANQP_ELEMENT_CIVIC_INFO_ID 0x10a
/** AP AP Location Public Identifier URI/FQDN */
#define ANQP_ELEMENT_FQDN_INFO_ID 0x10b

/** Events*/
#define EVENT_WLS_GENERIC 0x00000086
#define WLS_SUB_EVENT_FTM_COMPLETE 0
#define WLS_SUB_EVENT_RADIO_RECEIVED 1
#define WLS_SUB_EVENT_RADIO_RPT_RECEIVED 2
#define WLS_SUB_EVENT_ANQP_RESP_RECEIVED 3
#define WLS_SUB_EVENT_RTT_RESULTS 4

/** Radio Measurement Request Element IDs*/
#define MEASUREMENT_REQUEST_ELEMENT_ID 0x26
#define FTM_RANGE_REQUEST 16
#define NEIGHBOR_REPORT_ELEMENT_ID 0x34 //(52)
#define WIDE_BW_CHANNEL_SUB_ELEMENT_ID 0x6

/** Radio Measurement Report Frame*/
#define MEASUREMENT_REPORT_ELEMENT_ID 39
#define RADIO_MEASUREMENT_CATEGORY_PUBLIC_ACTION_FRAME 5

/** Radio Measurement Request Action IDs*/
enum radio_measurement_action {
	RADIO_MEASUREMENT_REQUEST = 0,
	RADIO_MEASUREMENT_REPORT,
	LINK_MEASUREMENT_REQUEST,
	LINK_MEASUREMENT_REPORT,
	NEIGHBOR_REPORT_REQUEST,
	NEIGHBOR_REPORT_RESPONSE,
};

/** Custom events definitions */
/** AP connected event */
#define CUS_EVT_AP_CONNECTED "EVENT=AP_CONNECTED"
/** Custom events definitions end */

/*TLVs*/
/** TLV  type ID definition */
#define PROPRIETARY_TLV_BASE_ID 0x0100
#define FTM_SESSION_CFG_INITATOR_TLV_ID (PROPRIETARY_TLV_BASE_ID + 273)
#define FTM_NTB_RANGING_CFG_TLV_ID (PROPRIETARY_TLV_BASE_ID + 343)
#define FTM_TB_RANGING_CFG_TLV_ID (PROPRIETARY_TLV_BASE_ID + 344)
#define FTM_RANGE_REPORT_TLV_ID                                                \
	(PROPRIETARY_TLV_BASE_ID + 0x10C) /* 0x0100 + 0x10C = 0x20C */
#define FTM_SESSION_CFG_LCI_TLV_ID (PROPRIETARY_TLV_BASE_ID + 270)
#define FTM_SESSION_CFG_LOCATION_CIVIC_TLV_ID (PROPRIETARY_TLV_BASE_ID + 271)

/** ANQP REQ FRAME */
#define GAS_INITIAL_REQUEST 0xa
#define GAS_INITIAL_RESPONSE 0xb
#define ADVERTISEMENT_PROTO 0x6c
#define GAS_COMEBACK_REQUEST 0x0c
#define GAS_COMEBACK_RESPONSE 0x0d

/** Radio Measurement Action */
#define RADIO_MEASUREMENT_REQUEST 0
#define RADIO_MEASUREMENT_REPORT 1
#define LINK_MEASUREMENT_REQUEST 2
#define LINK_MEASUREMENT_REPORT 3
#define NEIGHBOR_REPORT_REQUEST 4
#define NEIGHBOR_REPORT_RESPONSE 5

/** Action category: Public Action frame */
#define WIFI_CATEGORY_PUBLIC_ACTION_FRAME 4

/** Structure of command table*/
typedef struct {
	/** User Command ID*/
	int cmd_id;
	/** Command name */
	char *cmd;
	/** Command function pointer */
	int (*func)(int argc, char *argv[], void *param);
	/** Command usuage */
	char **help;
} wls_app_command_table;

/** Structure of FTM_SESSION_CFG_NTB_RANGING / FTM_SESSION_CFG_TB_RANGING TLV
 * data*/
typedef struct _ranging_cfg {
	/** Indicates the channel BW for session*/
	/*0: HE20, 1: HE40, 2: HE80, 3: HE80+80, 4: HE160, 5:HE160_SRF*/
	t_u8 format_bw;
	/** indicates for bandwidths less than or equal to 80 MHz the maximum
	 * number of space-time streams to be used in DL/UL NDP frames in the
	 * session*/
	t_u8 max_i2r_sts_upto80;
	/**indicates for bandwidths less than or equal to 80 MHz the maximum
	 * number of space-time streams to be used in DL/UL NDP frames in the
	 * session*/
	t_u8 max_r2i_sts_upto80;
	/**Specify measurement freq in Hz to calculate measurement interval*/
	t_u8 az_measurement_freq;
	/**Indicates the number of measurements to be done for session*/
	t_u8 az_number_of_measurements;
	/** Initator lmr feedback */
	t_u8 i2r_lmr_feedback;
	/**Include location civic request (Expect location civic from
	 * responder)*/
	t_u8 civic_req;
	/**Include LCI request (Expect LCI info from responder)*/
	t_u8 lci_req;
} __ATTRIB_PACK__ ranging_cfg_t;

/** Structure of FTM_SESSION_CFG TLV data*/
typedef struct _ftm_session_cfg {
	/** Indicates how many burst instances are requested for the FTM
	 * session*/
	t_u8 burst_exponent;
	/** Indicates the duration of a burst instance*/
	t_u8 burst_duration;
	/**Minimum time between consecutive FTM frames*/
	t_u8 min_delta_FTM;
	/**ASAP/non-ASAP casel*/
	t_u8 is_ASAP;
	/**Number of FTMs per burst*/
	t_u8 per_burst_FTM;
	/**FTM channel spacing: HT20/HT40/VHT80/*/
	t_u8 channel_spacing;
	/**Indicates the interval between two consecutive burst instances*/
	t_u16 burst_period;
} __ATTRIB_PACK__ ftm_session_cfg_t;

/** Structure for FTM_SESSION_CFG_LOCATION_CIVIC TLV data*/
typedef struct _civic_loc_cfg {
	/**Civic location type*/
	t_u8 civic_location_type;
	/**Country code*/
	t_u16 country_code;
	/**Civic address type*/
	t_u8 civic_address_type;
	/**Civic address length*/
	t_u8 civic_address_length;
	/**Civic Address*/
	t_u8 civic_address[256];
} __ATTRIB_PACK__ civic_loc_cfg_t;

/** Structure for FTM_SESSION_CFG_LCI TLV data*/
typedef struct _lci_cfg {
	/** known longitude*/
	double longitude;
	/** known Latitude*/
	double latitude;
	/** known altitude*/
	double altitude;
	/** known Latitude uncertainty*/
	t_u8 lat_unc;
	/** known Longitude uncertainty*/
	t_u8 long_unc;
	/** Known Altitude uncertainty*/
	t_u8 alt_unc;
	/** 1 word for additional Z information */
	t_u32 z_info;
} __ATTRIB_PACK__ lci_cfg_t;

/** Structure for FTM_SESSION_CFG_NTB_RANGING TLV*/
typedef struct _ranging_cfg_tlv {
	/** Type*/
	t_u16 type;
	/** Length*/
	t_u16 len;
	/** Value*/
	ranging_cfg_t val;
} __ATTRIB_PACK__ ranging_cfg_tlv_t;

/** Structure for FTM_SESSION_CFG  TLV*/
typedef struct _ftm_session_cfg_tlv {
	/** Type*/
	t_u16 type;
	/** Length*/
	t_u16 len;
	/** Value*/
	ftm_session_cfg_t val;
	/** Civic request */
	t_u8 civic_req;
	/** LCI req */
	t_u8 lci_req;
} __ATTRIB_PACK__ ftm_session_cfg_tlv_t;

/** Structure for FTM_SESSION_CFG_LOCATION_CIVIC TLV*/
typedef struct _civic_loc_tlv {
	/** Type*/
	t_u16 type;
	/** Length*/
	t_u16 len;
	/** Value*/
	civic_loc_cfg_t val;
} __ATTRIB_PACK__ civic_loc_tlv_t;

/** Structure for FTM_SESSION_CFG_LCI TLV*/
typedef struct _lci_tlv {
	/** Type*/
	t_u16 type;
	/** Length*/
	t_u16 len;
	/** Value*/
	lci_cfg_t val;
} __ATTRIB_PACK__ lci_tlv_t;

/** Structure for DOT11MC FTM_SESSION_CFG */
typedef struct _dot11mc_ftm_cfg {
	/** FTM session cfg*/
	ftm_session_cfg_tlv_t sess_tlv;
	/** Location Request cfg*/
	lci_tlv_t lci_tlv;
	/** Civic location cfg*/
	civic_loc_tlv_t civic_tlv;

} __ATTRIB_PACK__ dot11mc_ftm_cfg_t;

/** Structure for DOT11AZ FTM_SESSION_CFG */
typedef struct _dot11az_ftmcfg_ntb_t {
	/** NTB session cfg */
	ranging_cfg_tlv_t range_tlv;
} __ATTRIB_PACK__ dot11az_ftm_cfg_t;

/** Type definition for ANQP request action frames */
typedef struct _advertisement_proto {
	/** Peer mac address */
	t_u8 id;
	/** Peer mac address */
	t_u8 len;
	/** Category */
	t_u16 tuple[1];
} __ATTRIB_PACK__ advertisement_proto;

#define MAX_QUERY_ELEMENT 5
/** Type definition for ANQP request action frames */
typedef struct _anqp_query_list {
	/** Peer mac address */
	t_u16 req_len;
	/** Peer mac address */
	t_u16 info_id;
	/** Peer mac address */
	t_u16 len;
	/** Category */
	t_u16 list[MAX_QUERY_ELEMENT];
} __ATTRIB_PACK__ anqp_query_list;

/** Type definition for ANQP request action frames */
typedef struct _anqp_req_frame {
	/** Peer mac address */
	t_u8 peer_mac_addr[ETH_ALEN];
	/** Category */
	t_u8 category;
	/** Action */
	t_u8 action;
	/** Dialog taken */
	t_u8 dialog_token;
	/** Channel  */
	t_u8 channel;
	/** IE List of TLVs */
	t_u8 ie_list[256];
} __ATTRIB_PACK__ anqp_req_t;

/** Type definition for ANQP Comeback request action frames */
typedef struct _anqp_comeback_req {
	/** Peer mac address */
	t_u8 peer_mac_addr[ETH_ALEN];
	/** Category */
	t_u8 category;
	/** Action */
	t_u8 action;
	/** Dialog taken */
	t_u8 dialog_token;
	/** IE List of TLVs */
	t_u8 ie_list[256];
} __ATTRIB_PACK__ anqp_comeback_req_t;

/** Type definition for generic Hostcmd for dot11mc request action frames */
typedef struct _neighbor_report_req {
	/** 0:Get, 1:Set */
	t_u16 action;
	/** Dialog taken */
	t_u8 dialog_token;
	/** LCI Req */
	t_u8 lci_req;
	/** Civic Req */
	t_u8 loc_civic_req;
} __ATTRIB_PACK__ neighbor_report_req_t;

typedef struct _gas_initial_resp {
	/** Category */
	t_u8 category;
	/** Action */
	t_u8 action;
	/** Dialog token */
	t_u8 DialogToken;
	/** Status */
	t_u8 Status;
	/** Gas Comeback Delay */
	t_u16 GasComebackDelay;
	/** Advertisement Protocol ID */
	t_u8 AdvtProtocolIE;
	/** Query Response Len */
	t_u16 QueryRspLen;
	/** Query Response  */
	t_u8 QueryRsp[0];
} __ATTRIB_PACK__ gas_initial_resp_t;

typedef struct _gas_comeback_resp {
	/** Category */
	t_u8 category;
	/** Action */
	t_u8 action;
	/** Dialog token */
	t_u8 DialogToken;
	/** Status */
	t_u8 Status;
	/** Fragment ID */
	t_u8 frag_id;
	/** Gas Comeback Delay */
	t_u16 GasComebackDelay;
	/** Advertisement Protocol ID */
	t_u8 AdvtProtocolIE;
	/** Query Response Len */
	t_u16 QueryRspLen;
	/** Query Response  */
	t_u8 QueryRsp[0];
} __ATTRIB_PACK__ gas_comeback_resp_t;

typedef struct _location_response_frame {
	t_u8 mac[6];
	/** Category */
	t_u8 category;
	/** Action */
	t_u8 action;
	/** Dialog token */
	t_u8 dialog_token;
	t_u8 ie_list[0];
} __ATTRIB_PACK__ location_response_frame_t;

typedef struct _action_frame_header {
	/** Category */
	t_u8 category;
	/** Action */
	t_u8 action;
} __ATTRIB_PACK__ action_frame_header_t;

typedef union _gas_frames {
	action_frame_header_t hdr;
	gas_initial_resp_t init_resp;
	gas_comeback_resp_t cbk_resp;
} __ATTRIB_PACK__ gas_frames_t;

/** Type definition for hostcmd_ftm_session_cfg */
typedef struct _hostcmd_ftm_session_cfg {
	/** 0:Get, 1:Set */
	t_u16 action;
	/** FTM_SESSION_CFG_TLVs*/
	union {
		/**11az cfg*/
		dot11az_ftm_cfg_t cfg_11az;
		/** 11mc cfg*/
		dot11mc_ftm_cfg_t cfg_11mc;
	} tlv;
} __ATTRIB_PACK__ hostcmd_ftm_session_cfg;

/** Type definition for hostcmd_ftm_session_ctrl */
typedef struct _hostcmd_ftm_session_ctrl {
	/** 0: Not used, 1: Start, 2: Stop*/
	t_u16 action;
	/*FTM for ranging*/
	t_u8 for_ranging;
	/** Mac address of the peer with whom FTM session is required*/
	t_u8 peer_mac[ETH_ALEN];
	/** Channel on which FTM must be started */
	t_u8 chan;
} __ATTRIB_PACK__ hostcmd_ftm_session_ctrl;

/** Type definition for hostcmd_80211_action_frame */
typedef struct _hostcmd_80211_action_frame {
	/** Mac address of the peer */
	t_u8 peer_mac[ETH_ALEN];
	/** category field */
	t_u8 category;
	/** Action */
	t_u8 action;
	/** Token */
	t_u8 dialog_token;
	/** IE list variable */
	t_u8 ie_list[];
} __ATTRIB_PACK__ hostcmd_80211_action_frame;

/** Type definition for generic Hostcmd for 11AZ FTM Session */
typedef struct _hostcmd_ds_ftm_session_cmd {
	/** HostCmd_DS_GEN */
	HostCmd_DS_GEN cmd_hdr;
	/** Command Body */
	union {
		/** hostcmd for session_ctrl user command */
		hostcmd_ftm_session_ctrl ftm_session_ctrl;
		/** hostcmd for session_cfg user command */
		hostcmd_ftm_session_cfg ftm_session_cfg;
		/** hostcmd for action frame command */
		hostcmd_80211_action_frame action_frame;

	} cmd;
} __ATTRIB_PACK__ hostcmd_ds_ftm_session_cmd;

/** HostCmd ANQP Action frames*/
typedef struct _hostcmd_ds_anqp_req {
	/** HostCmd_DS_GEN */
	HostCmd_DS_GEN cmd_hdr;
	union {
		/** Command Body for ANQP request action frame  */
		anqp_req_t anqp_req;
		/** Command Body for ANQP comeback request action frame  */
		anqp_comeback_req_t anqp_cbak_req;
	} cmd;
} __ATTRIB_PACK__ hostcmd_anqp_req_t;

/** HostCmd Neighbor report request */
typedef struct _hostcmd_ds_nbor_req {
	/** HostCmd_DS_GEN */
	HostCmd_DS_GEN cmd_hdr;
	/** Command Body */
	/** hostcmd for neighbor report request action frame*/
	neighbor_report_req_t nbor_req;
} __ATTRIB_PACK__ hostcmd_nbor_req_t;

/** Type definition for FTM Session Events */

/** Event ID length */
#define EVENT_ID_LEN 4

/**Structure for RTT results subevent*/
typedef struct _wls_subevent_rtt_results_t {
	/** complete */
	t_u8 complete;
	/** tlv buffer */
	/** MrvlIEtypes_RTTResult_t */
	t_u8 tlv_buffer[];
} __ATTRIB_PACK__ wls_subevent_rtt_results_t;

/**Structure for ANQP Resp subevent*/
typedef struct _anqp_resp_event {
	/** MAC address of the responder */
	t_u8 mac[6];
	/**Length*/
	t_u8 len;
	/** response buffer*/
	t_u8 buffer[0];
} __ATTRIB_PACK__ anqp_resp_event_t;

/**Structure for FTM complete subevent*/
typedef struct _wls_subevent_ftm_complete {
	/** BSS Number */
	t_u8 bssNum;
	/** BSS Type */
	t_u8 bssType;
	/** MAC address of the responder */
	t_u8 mac[ETH_ALEN];
	/** Average RTT */
	t_u32 avg_rtt;
	/** Average Clock offset */
	t_u32 avg_clk_offset;
	/** Measure start timestamp */
	t_u32 meas_start_tsf;
} __ATTRIB_PACK__ wls_subevent_ftm_complete_t;

/**  FTM range request data */
typedef struct _ftm_range_req {
	/** BSSID */
	t_u8 bssid[6];
	/** Channel */
	t_u8 channel;
	/** BW */
	t_u8 bandwidth;
	/** measurement start time */
	t_u32 start_time;
	/** Range */
	t_u32 range;
} __ATTRIB_PACK__ ftm_range_request;

/**  BSSID cap field*/
typedef struct _bss_capab {
	/** Reachability */
	t_u8 spectrumMgmt : 1;
	/** Qos */
	t_u8 QoS : 1;
	/** apsd */
	t_u8 apsd : 1;
	/** radio measurement */
	t_u8 radioMesurement : 1;
	/** delayedBlockAck */
	t_u8 delayedBlockAck : 1;
	/** immeBlockAck */
	t_u8 immeBlockAck : 1;
} __ATTRIB_PACK__ bss_capab;

/**  BSSID Info field */
typedef struct _bssid_info {
	/** APreachability */
	t_u32 APreachability : 2;
	/** security */
	t_u32 security : 1;
	/** Key scope */
	t_u32 keyScope : 1;
	/** bss cap */
	t_u32 spectrumMgmt : 1;
	/** Qos */
	t_u32 QoS : 1;
	/** apsd */
	t_u32 apsd : 1;
	/** radio measurement */
	t_u32 radioMesurement : 1;
	/** delayedBlockAck */
	t_u32 delayedBlockAck : 1;
	/** immeBlockAck */
	t_u32 immeBlockAck : 1;
	/** mobility domain */
	t_u32 mobilityDomain : 1;
	/** HT */
	t_u32 highThroughput : 1;
	/** VHT */
	t_u32 veryhighThroughput : 1;
	/** FTM */
	t_u32 FTM : 1;
	/** reserved */
	t_u32 reserved : 18;
} __ATTRIB_PACK__ bssid_info_t;

/**  Wide bandwidth channel subelement*/
typedef struct _wide_bw_chan_subelem {
	/** ID field*/
	t_u8 id;
	/** len */
	t_u8 len;
	/** BW */
	t_u8 bw[3];
} __ATTRIB_PACK__ wide_bw_chan_subelem_subelem;

/**  Neighbor Request element*/
typedef struct _neighbor_request_element {
	/** ID field*/
	t_u8 id;
	/** len */
	t_u8 len;
	/** bssid */
	t_u8 bssid[6];
	/** bss info */
	bssid_info_t bssid_info;
	/** op class*/
	t_u8 operating_class;
	/** Channel  */
	t_u8 channel;
	/** Phy type */
	t_u8 phy_type;
	/** option sub element*/
	t_u8 optionalsubelem[];
} __ATTRIB_PACK__ neighbor_request_element;

/**FTM Range Request Element*/
typedef struct _ftm_range_request_element {
	/** Radomization Interval field */
	t_u16 random_interval;
	/** AP count */
	t_u8 min_ap_count;
	/** Range Request subelements*/
	t_u8 ftm_req_subelem[];
} __ATTRIB_PACK__ ftm_range_request_element;

/**Structure for Measurement Request element request mode field*/
typedef struct _request_mode_t {
	/** To indicate multiple measurements to be started in parallel */
	t_u8 parallel : 1;
	/** To differentiate between a request to do measurements /to control
	 * the measurement reports*/
	t_u8 enable : 1;
	/** 1 : Accept measurement request */
	t_u8 request : 1;
	/** 1 : Accepts autonomous reports */
	t_u8 report : 1;
	/** 1 : Duration request mandatory */
	t_u8 duration : 1;
	/** reserved */
	t_u8 reserved : 3;
} __ATTRIB_PACK__ request_mode_t;

/**Structure for Measurement Request element field*/
typedef struct _range_entry_field {
	/** start time */
	t_u32 meas_start_time;
	/** bssid */
	t_u8 bssid[6];
	/** range measured */
	t_u32 range : 24;
	/** max exponent */
	t_u8 max_range_exponent;
	/** rsvd */
	t_u8 reserved;
} __ATTRIB_PACK__ range_entry_field;

/**Structure for Measurement Request element subelemtent field*/
typedef struct _error_entry_field {
	/** start time */
	t_u32 meas_start_time;
	/** bssid */
	t_u8 bssid[6];
	/** error code */
	t_u8 error_code;
} __ATTRIB_PACK__ error_entry_field;

/**Structure for Measurement Report element*/
typedef struct _ftm_range_report_element {
	/** num of aps */
	t_u8 entry_count;
	/** range entry */
	range_entry_field range_entry[64]; // MAX_RANGE_REQ * size of
					   // range_entry_field
	/** error  count */
	t_u8 error_entry_count;
	/** error entry */
	error_entry_field error_entry[64]; // MAX_RANGE_REQ * size of
					   // error_entry_field
	/** other subelement */
	t_u8 ftm_report_subelem[];
} __ATTRIB_PACK__ ftm_range_report_element;

/**Structure for Measurement Report  mode field*/
typedef struct _report_mode_t {
	/** late */
	t_u8 late : 1;
	/** incapable */
	t_u8 incapable : 1;
	/** refused */
	t_u8 refused : 1;
	/** rsvd */
	t_u8 reserved : 5;
} __ATTRIB_PACK__ report_mode_t;

/**Structure for Measurement Report element*/
typedef struct _meas_report_element {
	/** element id */
	t_u8 element_id;
	/** length */
	t_u8 length;
	/** toekn */
	t_u8 token;
	/** report mode */
	report_mode_t report_mode;
	/** type of measurement */
	t_u8 type;
	/** report  */
	t_u8 report[];
} __ATTRIB_PACK__ meas_report_element;

/**Structure for Measurement Report frame*/
typedef struct _meas_report_frame {
	/** mac */
	t_u8 mac[6];
	/** category */
	t_u8 category;
	/** action */
	t_u8 action;
	/** token */
	t_u8 dialog_token;
	/** ie */
	t_u8 ie_list[];
} __ATTRIB_PACK__ meas_report_frame;

/**Structure for Measurement Request element*/
typedef struct _meas_request_element {
	/** id */
	t_u8 element_id;
	/** length */
	t_u8 length;
	/** token */
	t_u8 token;
	/** mode */
	request_mode_t request_mode;
	/** type */
	t_u8 type;
	/** req */
	t_u8 request[];
} __ATTRIB_PACK__ meas_request_element;

/**Structure for Radio Measurement Request subevent*/
typedef struct _wls_subevent_radio_meas_req {
	/** TSF Low */
	t_u32 tsf_low;
	/** BSSID  */
	t_u8 mac[ETH_ALEN];
	/** length */
	t_u8 len;
	/** buffer */
	t_u8 buf[];
} __ATTRIB_PACK__ wls_subevent_radio_meas_req_t;

/** TLV for FTM Range Report */
typedef struct _range_report_tlv_t {
	/**Type*/
	t_u16 type;
	/**Length*/
	t_u16 len;
	/** MAC address of the responder */
	t_u8 mac[ETH_ALEN];
	/** Average RTT */
	t_u32 avg_rtt;
	/** Average Clock offset */
	t_u32 avg_clk_offset;
	/** LCI and Location Civic TLV */
} __ATTRIB_PACK__ range_report_tlv_t;

/** Structure for FTM events*/
typedef struct _wls_event_t {
	/** Event ID */
	t_u16 event_id;
	/** BSS index number for multiple BSS support */
	t_u8 bss_index;
	/** BSS type */
	t_u8 bss_type;
	/** sub event id */
	t_u8 sub_event_id;
	union {
		/** FTM Complete Sub event*/
		wls_subevent_ftm_complete_t ftm_complete;
		wls_subevent_radio_meas_req_t radio_req;
		anqp_resp_event_t anqp_resp;
	} e;
} __ATTRIB_PACK__ wls_event_t;

/*Application Global Data*/
typedef struct {
	/** Average RTT */
	t_u32 avg_rtt;
	/** Average Clock offset */
	t_u32 avg_clk_offset;
	/*Range*/
	t_s64 range;
} range_results_t;

/**
 * struct pasn_peer to hold BSSID
 */
struct pasn_peer {
	t_u8 bssid[ETH_ALEN];
};

/**
 * struct pasn_auth - PASN authentication trigger parameters
 *
 * These are used across the PASN authentication event from the driver to
 * userspace and to send a response to it.
 * @action: Action type. Only significant for the event interface.
 * @num_peers: The number of peers for which the PASN handshake is requested
 *	for.
 * @peer: Holds the peer details.
 */
typedef struct {
	enum {
		PASN_ACTION_AUTH = 1,
		PASN_ACTION_DELETE_SECURE_RANGING_CONTEXT,
	} action;
	unsigned int num_peers;
	struct pasn_peer peer[MAX_PASN_PEERS];
} pasn_auth_cmd_t;

/** Structure for ftm command private data*/
typedef struct _wls_app_data {
	/** 0 : 80211mc, 1:80211az NTB 2:11AZ TB*/
	t_u8 protocol_type;
	/** num of times to run FTM*/
	t_u8 loop_cnt;
	/** flag to run nonstop*/
	t_u8 run_nonstop;
	/** flag to indicate ftm start from radio measurement request*/
	t_u8 is_radio_request;
	/** flag to indicate ongoing ftm radio measurement request*/
	t_u8 is_range_req_in_progress;
	/** Minimum ap count in range request*/
	t_u8 range_ap_count;
	/** Minimum ap count in range request*/
	t_u8 current_range_req_idx;
	/** Range req dialog token */
	t_u8 range_req_dialog_token;
	/** flag is associated */
	t_u8 associated;
	/** 0 - STA, 1- AP*/
	t_u8 bss_type;
	/**flag for ftm started */
	t_u8 ftm_started;
	/** flag for app to terminate ftm session*/
	t_u8 terminate_app;
	/**flag for debug print level */
	t_u8 debug_level;
	/**peer mac address */
	t_u8 peer_mac[ETH_ALEN];
	/**AP mac address */
	t_u8 ap_mac[ETH_ALEN];
	/** Channel number for FTM session*/
	t_u8 channel;
	/**CFG cmd action code 1: SET/0: GET action */
	/**CTRL cmd action code  1: associated_nonsecure_ftm,
	 * 2:associated_secure_ftm*/
	/** 3: unassociated_nonsecure_ftm, 4:unassociated_secure_ftm*/
	/*PASN cmd action code : 1: Start 2: Stop*/
	t_u8 hostcmd_action;
	/**Is LCI data available in cfg*/
	t_u8 lci_request;
	/** Is civic data available in cfg*/
	t_u8 civic_request;
	/** Is fqdn query in anqp query list*/
	t_u8 fqdn;
	/** ANQP request frame elements*/
	t_u8 anqp_dialog_token;
	/** Neighbor report request frame elements*/
	t_u8 nbor_dialog_token;
	/**ntb cfg param*/
	ranging_cfg_t range_cfg;
	/** 11mc session cfg param*/
	ftm_session_cfg_t session_cfg;
	/** lci cfg data*/
	lci_cfg_t lci_cfg;
	/** civic cfg data - this should be last field*/
	civic_loc_cfg_t civic_cfg;
	/**CSI processing config*/
	hal_wls_processing_input_params_t wls_processing_input;
} wls_app_data_t;

int mlanwls_main(int argc, char *argv[]);
extern int process_wls_generic_event(t_u8 *buffer, t_u16 size, char *if_name);

#endif /* _WLS_H_ */
