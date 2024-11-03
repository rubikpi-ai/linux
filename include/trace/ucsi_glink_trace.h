/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM ucsi_glink

#if !defined(_TRACE_UCSI_GLINK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_UCSI_GLINK_H

#include <linux/tracepoint.h>

TRACE_EVENT(ucsi_glink,
	TP_PROTO(char *prefix, char *msg_type, char *msg),
	TP_ARGS(prefix, msg_type, msg),
	TP_STRUCT__entry(
		__string(prefix, prefix)
		__string(msg_type, msg_type)
		__string(msg, msg)
	),
	TP_fast_assign(
		__assign_str(prefix, prefix);
		__assign_str(msg_type, msg_type);
		__assign_str(msg, msg);
	),
	TP_printk("%s %s %s", __get_str(prefix), __get_str(msg_type), __get_str(msg))
);

#endif /* _TRACE_UCSI_GLINK_H */

/* This part must be outside protection */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE ucsi_glink_trace

#include <trace/define_trace.h>
