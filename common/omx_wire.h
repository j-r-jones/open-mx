#ifndef __omx_wire_h__
#define __omx_wire_h__

/******************************
 * Packet definition
 */

#define ETH_P_OMX 0x86DF

enum omx_pkt_type {
	/* must start with NONE and end with MAX */
	OMX_PKT_TYPE_NONE=0,
	OMX_PKT_TYPE_RAW, /* FIXME: todo */
	OMX_PKT_TYPE_MFM_NIC_REPLY, /* FIXME: todo */
	OMX_PKT_TYPE_HOST_QUERY, /* FIXME: todo */
	OMX_PKT_TYPE_HOST_REPLY, /* FIXME: todo */

	OMX_PKT_TYPE_ETHER_UNICAST = 32, /* FIXME: todo */
	OMX_PKT_TYPE_ETHER_MULTICAST, /* FIXME: todo */
	OMX_PKT_TYPE_ETHER_NATIVE, /* FIXME: todo */
	OMX_PKT_TYPE_TRUC, /* FIXME: todo */
	OMX_PKT_TYPE_CONNECT, /* FIXME: todo */
	OMX_PKT_TYPE_TINY,
	OMX_PKT_TYPE_SMALL,
	OMX_PKT_TYPE_MEDIUM,
	OMX_PKT_TYPE_RENDEZ_VOUS,
	OMX_PKT_TYPE_PULL,
	OMX_PKT_TYPE_PULL_REPLY,
	OMX_PKT_TYPE_NOTIFY, /* FIXME: todo */
	OMX_PKT_TYPE_NACK_LIB, /* FIXME: todo */
	OMX_PKT_TYPE_NACK_MCP, /* FIXME: todo */

	OMX_PKT_TYPE_MAX=255,
};

static inline const char*
omx_strpkttype(enum omx_pkt_type ptype)
{
	switch (ptype) {
	case OMX_PKT_TYPE_NONE:
		return "None";
	case OMX_PKT_TYPE_RAW:
		return "Raw";
	case OMX_PKT_TYPE_MFM_NIC_REPLY:
		return "MFM Nic Reply";
	case OMX_PKT_TYPE_HOST_QUERY:
		return "Host Query";
	case OMX_PKT_TYPE_HOST_REPLY:
		return "Host Reply";
	case OMX_PKT_TYPE_ETHER_UNICAST:
		return "Ether Unicast";
	case OMX_PKT_TYPE_ETHER_MULTICAST:
		return "Ether Multicast";
	case OMX_PKT_TYPE_ETHER_NATIVE:
		return "Ether Native";
	case OMX_PKT_TYPE_TRUC:
		return "Truc";
	case OMX_PKT_TYPE_CONNECT:
		return "Connect";
	case OMX_PKT_TYPE_TINY:
		return "Tiny";
	case OMX_PKT_TYPE_SMALL:
		return "Small";
	case OMX_PKT_TYPE_MEDIUM:
		return "Medium";
	case OMX_PKT_TYPE_RENDEZ_VOUS:
		return "Rendez Vous";
	case OMX_PKT_TYPE_PULL:
		return "Pull";
	case OMX_PKT_TYPE_PULL_REPLY:
		return "Pull Reply";
	case OMX_PKT_TYPE_NOTIFY:
		return "Notify";
	case OMX_PKT_TYPE_NACK_LIB:
		return "Nack Lib";
	case OMX_PKT_TYPE_NACK_MCP:
		return "Nack MCP";
	default:
		return "** Unknown **";
	}
}

#include <linux/if_ether.h>

struct omx_pkt_head {
	struct ethhdr eth;
	uint16_t sender_peer_index; /* FIXME: unused */
	/* 16 */
};

struct omx_pkt_msg {
	uint8_t ptype;
	uint8_t dst_endpoint;
	uint8_t src_endpoint;
	uint8_t src_generation; /* FIXME: unused ? */
	uint16_t length;
	uint16_t pad2;
	uint16_t lib_seqnum;
	uint16_t lib_piggyack; /* FIXME: unused ? */
	uint32_t match_a;
	uint32_t match_b;
	uint32_t session; /* FIXME: unused ? */
	/* 24 */
};

/* 16 Bytes */
struct omx_pkt_connect {
	uint8_t ptype;
	uint8_t dest_endpoint;
	uint8_t src_endpoint;
	uint8_t src_generation; /* FIXME: unused ? */
	uint8_t length;
	uint8_t pad[3];
	uint16_t lib_seqnum;
	uint16_t dest_peer_index;
	uint32_t src_mac_low32;
};

struct omx_pkt_medium_frag {
	struct omx_pkt_msg msg;
	uint16_t frag_length;
	uint8_t frag_seqnum;
	uint8_t frag_pipeline;
	uint32_t pad;
};

struct omx_pkt_pull_request {
	uint8_t ptype;
	uint8_t dst_endpoint;
	uint8_t src_endpoint;
	uint8_t src_generation; /* FIXME: unused ? */
	uint32_t session; /* FIXME: unused ? */
	uint32_t length; /* FIXME: 64bits ? */
	uint32_t puller_rdma_id;
	uint32_t puller_offset; /* FIXME: 64bits ? */
	uint32_t pulled_rdma_id;
	uint32_t pulled_offset; /* FIXME: 64bits ? */
	uint32_t src_pull_handle; /* sender's handle id */
	uint32_t src_magic; /* sender's endpoint magic */
#if 0
	uint32_t total_length; /* full message total length */
	uint8_t rdmawin_id; /* target window id */
	uint8_t rdmawin_seqnum; /* target window seqnum */
	uint16_t rdma_offset; /* offset in target window first page */
	uint16_t offset; /* sender's first page offset */
	uint16_t pull_length; /* this pull length (pull_reply * pagesize) - target offset */
	uint32_t index; /* pull interation index (page_nr/page_per_pull) */
#endif
};

struct omx_pkt_pull_reply {
	uint8_t ptype;
	uint8_t pad[3];
	uint32_t length; /* FIXME: 64bits ? */
	uint32_t puller_rdma_id;
	uint32_t puller_offset; /* FIXME: 64bits ? */
	uint32_t dst_pull_handle; /* sender's handle id */
	uint32_t dst_magic; /* sender's endpoint magic */
#if 0
	uint8_t frame_seqnum; /* sender's pull index + page number in this frame */
	uint16_t frame_length; /* pagesize - frame_offset */
	uint32_t msg_offset; /* index * pagesize - target_offset + sender_offset */
#endif
};

struct omx_hdr {
	struct omx_pkt_head head;
	/* 32 */
	union {
		struct omx_pkt_msg generic;
		struct omx_pkt_msg tiny;
		struct omx_pkt_msg small;
		struct omx_pkt_medium_frag medium;
		struct omx_pkt_pull_request pull;
		struct omx_pkt_pull_reply pull_reply;
	} body;
};

#define OMX_PKT_FROM_MATCH_INFO(_pkt, _match_info)			\
do {									\
	(_pkt)->match_a = (uint32_t) (_match_info >> 32);		\
	(_pkt)->match_b = (uint32_t) (_match_info & 0xffffffff);	\
} while (0)

#define OMX_MATCH_INFO_FROM_PKT(_pkt) (((uint64_t) (_pkt)->match_a) << 32) | ((uint64_t) (_pkt)->match_b)

#endif /* __omx_wire_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
