#undef TRACE_SYSTEM
#define TRACE_SYSTEM cdc_acm

#if !defined(__CDC_ACM_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __CDC_ACM_TRACE_H

#include <linux/tracepoint.h>
#include <linux/tty.h>

TRACE_EVENT(acm_tty_write,
	TP_PROTO(struct acm *acm, const unsigned char *buf, int count),
	TP_ARGS(acm, buf, count),
	TP_STRUCT__entry(
		__string(dev_name, dev_name(&acm->data->dev))
		__string(buf, buf)
		__field(int, count)
	),
	TP_fast_assign(
		__assign_str(dev_name, dev_name(&acm->data->dev));
		__assign_str(buf, buf);
		__entry->count = count;
	),
	TP_printk("[%s] Write %d bytes %s", __get_str(dev_name), __entry->count, __get_str(buf))
);

TRACE_EVENT(syno_eunit_ack_status,
	TP_PROTO(struct acm *acm, int ackId),
	TP_ARGS(acm, ackId),
	TP_STRUCT__entry(
		__string(dev_name, dev_name(&acm->data->dev))
		__field(int, ackId)
	),
	TP_fast_assign(
		__assign_str(dev_name, dev_name(&acm->data->dev));
		__entry->ackId = ackId;
	),
	TP_printk("[%s] Receive Ack %d", __get_str(dev_name), __entry->ackId)
);

#endif /* __CDC_ACM_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/usb/class

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE cdc-acm-trace

#include <trace/define_trace.h>
