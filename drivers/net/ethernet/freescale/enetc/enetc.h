/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/* Copyright 2017-2019 NXP */

#include <linux/timer.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/dma-mapping.h>
#include <linux/skbuff.h>
#include <linux/ethtool.h>
#include <linux/if_vlan.h>
#include <linux/phylink.h>
#include <linux/fsl/ntmp.h>
#include <linux/dim.h>
#include <net/xdp.h>
#include <net/tsn.h>

#include "enetc_hw.h"
#include "enetc4_hw.h"
#include "enetc_msg.h"

#define ENETC_MAC_MAXFRM_SIZE	9600
#define ENETC_MAX_MTU		(ENETC_MAC_MAXFRM_SIZE - \
				(ETH_FCS_LEN + ETH_HLEN + VLAN_HLEN))

/* i.MX95 supports jumbo frame, but it is recommended to set the max frame
 * size to 2000 bytes.
 */
#define ENETC4_MAC_MAXFRM_SIZE	2000
#define ENETC4_MAX_MTU		(ENETC4_MAC_MAXFRM_SIZE - \
				(ETH_FCS_LEN + ETH_HLEN + VLAN_HLEN))

#define ENETC_CBD_DATA_MEM_ALIGN 64

#define ENETC_INT_NAME_MAX	(IFNAMSIZ + 8)

struct enetc_tx_swbd {
	union {
		struct sk_buff *skb;
		struct xdp_frame *xdp_frame;
	};
	dma_addr_t dma;
	struct page *page;	/* valid only if is_xdp_tx */
	u16 page_offset;	/* valid only if is_xdp_tx */
	u16 len;
	enum dma_data_direction dir;
	u8 is_dma_page:1;
	u8 check_wb:1;
	u8 do_twostep_tstamp:1;
	u8 is_eof:1;
	u8 is_xdp_tx:1;
	u8 is_xdp_redirect:1;
	u8 qbv_en:1;
};

struct enetc_lso_t {
	bool	ipv6;
	bool	tcp;
	u8	l3_hdr_len;
	u8	hdr_len; /* LSO header length */
	u8	l3_start;
	u16	lso_seg_size;
	int	total_len; /* total data length, not include LSO header */
};

#define ENETC_1KB_SIZE			1024
#define ENETC_LSO_MAX_DATA_LEN		(256 * ENETC_1KB_SIZE)

#define ENETC_RX_MAXFRM_SIZE	ENETC_MAC_MAXFRM_SIZE
#define ENETC_RXB_TRUESIZE	2048 /* PAGE_SIZE >> 1 */
#define ENETC_RXB_PAD		NET_SKB_PAD /* add extra space if needed */
#define ENETC_RXB_DMA_SIZE	\
	(SKB_WITH_OVERHEAD(ENETC_RXB_TRUESIZE) - ENETC_RXB_PAD)
#define ENETC_RXB_DMA_SIZE_XDP	\
	(SKB_WITH_OVERHEAD(ENETC_RXB_TRUESIZE) - XDP_PACKET_HEADROOM)
#define ENETC_RS_MAX_BYTES	(ENETC_RXB_DMA_SIZE * (MAX_SKB_FRAGS + 1))

struct enetc_rx_swbd {
	dma_addr_t dma;
	struct page *page;
	u16 page_offset;
	enum dma_data_direction dir;
	u16 len;
};

/* ENETC overhead: optional extension BD + 1 BD gap */
#define ENETC_TXBDS_NEEDED(val)	((val) + 2)
/* max # of chained Tx BDs is 15, including head and extension BD */
#define ENETC_MAX_SKB_FRAGS	13
/* For ENETC 4, max # of chained Tx BDs is 63, including head and extension BD */
#define ENETC4_MAX_SKB_FRAGS	61
/* 3: 1 BD for head, 1 BD for optional extended BD and 1 BD gap */
#define ENETC_TX_STOP_THRESHOLD	(MAX_SKB_FRAGS + 3)

struct enetc_ring_stats {
	unsigned int packets;
	unsigned int bytes;
	unsigned int rx_alloc_errs;
	unsigned int xdp_drops;
	unsigned int xdp_tx;
	unsigned int xdp_tx_drops;
	unsigned int xdp_redirect;
	unsigned int xdp_redirect_failures;
	unsigned int recycles;
	unsigned int recycle_failures;
	unsigned int win_drop;
};

struct enetc_xdp_data {
	struct xdp_rxq_info rxq;
	struct bpf_prog *prog;
	int xdp_tx_in_flight;
};

#define ENETC_RX_RING_DEFAULT_SIZE	2048
#define ENETC_TX_RING_DEFAULT_SIZE	2048
#define ENETC_DEFAULT_TX_WORK		(ENETC_TX_RING_DEFAULT_SIZE / 2)

struct enetc_bdr_resource {
	/* Input arguments saved for teardown */
	struct device *dev; /* for DMA mapping */
	size_t bd_count;
	size_t bd_size;

	/* Resource proper */
	void *bd_base; /* points to Rx or Tx BD ring */
	dma_addr_t bd_dma_base;
	union {
		struct enetc_tx_swbd *tx_swbd;
		struct enetc_rx_swbd *rx_swbd;
	};
	char *tso_headers;
	dma_addr_t tso_headers_dma;
};

struct enetc_bdr {
	struct device *dev; /* for DMA mapping */
	struct net_device *ndev;
	void *bd_base; /* points to Rx or Tx BD ring */
	union {
		void __iomem *tpir;
		void __iomem *rcir;
	};
	u16 index;
	u16 prio;
	int bd_count; /* # of BDs */
	int next_to_use;
	int next_to_clean;
	union {
		struct enetc_tx_swbd *tx_swbd;
		struct enetc_rx_swbd *rx_swbd;
	};
	union {
		void __iomem *tcir; /* Tx */
		int next_to_alloc; /* Rx */
	};
	void __iomem *idr; /* Interrupt Detect Register pointer */

	int buffer_offset;
	struct enetc_xdp_data xdp;

	struct enetc_ring_stats stats;

	dma_addr_t bd_dma_base;
	u8 tsd_enable; /* Time specific departure */
	bool ext_en; /* enable h/w descriptor extensions */

	/* DMA buffer for TSO headers */
	char *tso_headers;
	dma_addr_t tso_headers_dma;
} ____cacheline_aligned_in_smp;

static inline void enetc_bdr_idx_inc(struct enetc_bdr *bdr, int *i)
{
	if (unlikely(++*i == bdr->bd_count))
		*i = 0;
}

static inline int enetc_bd_unused(struct enetc_bdr *bdr)
{
	if (bdr->next_to_clean > bdr->next_to_use)
		return bdr->next_to_clean - bdr->next_to_use - 1;

	return bdr->bd_count + bdr->next_to_clean - bdr->next_to_use - 1;
}

static inline int enetc_swbd_unused(struct enetc_bdr *bdr)
{
	if (bdr->next_to_clean > bdr->next_to_alloc)
		return bdr->next_to_clean - bdr->next_to_alloc - 1;

	return bdr->bd_count + bdr->next_to_clean - bdr->next_to_alloc - 1;
}

/* Control BD ring */
#define ENETC_CBDR_DEFAULT_SIZE	64
struct enetc_cbdr {
	void *bd_base; /* points to Rx or Tx BD ring */
	void __iomem *pir;
	void __iomem *cir;
	void __iomem *mr; /* mode register */

	int bd_count; /* # of BDs */
	int next_to_use;
	int next_to_clean;

	dma_addr_t bd_dma_base;
	struct device *dma_dev;
};

#define ENETC_TXBD(BDR, i) (&(((union enetc_tx_bd *)((BDR).bd_base))[i]))

static inline union enetc_rx_bd *enetc_rxbd_ext(union enetc_rx_bd *rxbd)
{
	return ++rxbd;
}

/* Credit-Based Shaper parameters */
struct enetc_cbs_tc_cfg {
	u8 tc;
	bool enable;
	u8 bw;
	u32 hi_credit;
	u32 lo_credit;
	u32 idle_slope;
	u32 send_slope;
	u32 tc_max_sized_frame;
	u32 max_interference_size;
};

struct enetc_cbs {
	u32 port_transmit_rate;
	u32 port_max_size_frame;
	u8 tc_nums;
	struct enetc_cbs_tc_cfg tc_cfg[0];
};

#define ENETC_REV1	0x1
#define ENETC_REV4	0x4
enum enetc_errata {
	ENETC_ERR_VLAN_ISOL	= BIT(0),
	ENETC_ERR_UCMCSWP	= BIT(1),
	ENETC_ERR_SG_DROP_CNT	= BIT(2),
};

#define ENETC_SI_F_PSFP BIT(0)
#define ENETC_SI_F_QBV  BIT(1)
#define ENETC_SI_F_QBU  BIT(2)
#define ENETC_SI_F_LSO	BIT(3)
#define ENETC_SI_F_RSC	BIT(4)

enum enetc_mac_addr_type {UC, MC, MADDR_TYPE};

#define ENETC_MADDR_HASH_TBL_SZ	64
struct enetc_mac_filter {
	union {
		char mac_addr[ETH_ALEN];
		DECLARE_BITMAP(mac_hash_table, ENETC_MADDR_HASH_TBL_SZ);
	};
	int mac_addr_cnt;
};

#define ENETC_VLAN_HT_SIZE	64

/* PCI IEP device data */
struct enetc_si {
	struct pci_dev *pdev;
	struct enetc_hw hw;
	enum enetc_errata errata;

	struct net_device *ndev; /* back ref. */

	struct enetc_cbdr cbd_ring;

	int num_rx_rings; /* how many rings are available in the SI */
	int num_tx_rings;
	int num_fs_entries;
	int num_rss; /* number of RSS buckets */
	unsigned short pad;
	int hw_features;
	int pmac_offset; /* Only valid for PSI that supports 802.1Qbu */
	struct enetc_cbs *ecbs;

	u64 clk_freq;
	struct netc_cbdr cbdr;
	struct dentry *debugfs_root;

	struct workqueue_struct *workqueue;
	struct work_struct rx_mode_task;
	struct work_struct msg_task;
	struct enetc_mac_filter mac_filter[MADDR_TYPE];
	struct mutex msg_lock; /* mailbox message lock */
	char msg_int_name[ENETC_INT_NAME_MAX];

	DECLARE_BITMAP(active_vlans, VLAN_N_VID);
	DECLARE_BITMAP(vlan_ht_filter, ENETC_VLAN_HT_SIZE);

	int (*set_rss_table)(struct enetc_si *si, const u32 *table, int count);
	int (*get_rss_table)(struct enetc_si *si, u32 *table, int count);

	/* Notice, only for VSI/VF to use */
	int (*vf_register_msg_msix)(struct enetc_si *si);
	void (*vf_free_msg_msix)(struct enetc_si *si);
	int (*vf_register_link_status_notify)(struct enetc_si *si, bool notify);
};

static inline bool is_enetc_rev1(struct enetc_si *si)
{
	return si->pdev->revision == ENETC_REV1;
}

static inline bool is_enetc_rev4(struct enetc_si *si)
{
	return si->pdev->revision == ENETC_REV4;
}

#define ENETC_SI_ALIGN	32

static inline void *enetc_si_priv(const struct enetc_si *si)
{
	return (char *)si + ALIGN(sizeof(struct enetc_si), ENETC_SI_ALIGN);
}

static inline bool enetc_si_is_pf(struct enetc_si *si)
{
	return !!(si->hw.port);
}

static inline int enetc_pf_to_port(struct pci_dev *pf_pdev)
{
	switch (pf_pdev->devfn) {
	case 0:
		return 0;
	case 1:
		return 1;
	case 2:
		return 2;
	case 6:
		return 3;
	default:
		return -1;
	}
}

static inline int enetc4_pf_to_port(struct pci_dev *pf_pdev)
{
	switch (pf_pdev->devfn) {
	case 0:
		return 0;
	case 64:
		return 1;
	case 128:
		return 2;
	default:
		return -1;
	}
}

#define ENETC_MAX_NUM_TXQS	8

struct enetc_int_vector {
	void __iomem *rbier;
	void __iomem *tbier_base;
	void __iomem *ricr0;
	void __iomem *ricr1;
	unsigned long tx_rings_map;
	int count_tx_rings;
	u32 rx_ictt;
	u16 comp_cnt;
	bool rx_dim_en, rx_napi_work;
	struct napi_struct napi ____cacheline_aligned_in_smp;
	struct dim rx_dim ____cacheline_aligned_in_smp;
	char name[ENETC_INT_NAME_MAX];

	struct enetc_bdr rx_ring;
	struct enetc_bdr tx_ring[];
} ____cacheline_aligned_in_smp;

struct enetc_cls_rule {
	struct ethtool_rx_flow_spec fs;
	u32 entry_id;
	int used;
};

#define ENETC_MAX_BDR_INT	6 /* fixed to max # of available cpus */
union psfp_cap {
	struct{
		u32 max_streamid;
		u32 max_psfp_filter;
		u32 max_psfp_gate;
		u32 max_psfp_gatelist;
		u32 max_psfp_meter;
	};
	struct {
		u32 max_rpt_entries;
		u32 max_isit_entries;
		u32 max_isft_entries;
		u32 max_ist_entries;
		u32 max_sgit_entries;
		u32 max_isct_entries;
		u32 sgcl_num_words;
	} ntmp; /* capability of NTMP PSFP tables */
};

struct enetc_psfp_node {
	struct ntmp_isit_cfg isit_cfg;
	u32 chain_index;
	u32 isf_eid;    /* hardware assigns entry ID */
	u32 rp_eid;     /* software assigns entry ID */
	u32 sgi_eid;    /* software assigns entry ID */
	u32 sgcl_eid;   /* software assigns entry ID */
	u32 isc_eid;    /* software assigns entry ID */
	struct flow_stats stats;
	struct hlist_node node;
};

struct enetc_psfp_cfg {
	struct ntmp_isit_cfg *isit_cfg;
	struct ntmp_ist_cfg *ist_cfg;
	struct ntmp_isft_cfg *isft_cfg;
	struct ntmp_sgit_cfg *sgit_cfg;
	struct ntmp_sgclt_cfg *sgclt_cfg;
	struct ntmp_isct_cfg *isct_cfg;
	struct ntmp_rpt_cfg *rpt_cfg;
};

#define ENETC_F_TX_TSTAMP_MASK	0xff
enum enetc_active_offloads {
	/* 8 bits reserved for TX timestamp types (hwtstamp_tx_types) */
	ENETC_F_TX_TSTAMP		= BIT(0),
	ENETC_F_TX_ONESTEP_SYNC_TSTAMP	= BIT(1),

	ENETC_F_RX_TSTAMP		= BIT(8),
	ENETC_F_QBV			= BIT(9),
	ENETC_F_QCI			= BIT(10),
	ENETC_F_QBU			= BIT(11),

	ENETC_F_CHECKSUM		= BIT(12),
	ENETC_F_LSO			= BIT(13),
	ENETC_F_RSC			= BIT(14),
};

enum enetc_flags_bit {
	ENETC_TX_ONESTEP_TSTAMP_IN_PROGRESS = 0,
	ENETC_TX_DOWN,
};

/* interrupt coalescing modes */
enum enetc_ic_mode {
	/* one interrupt per frame */
	ENETC_IC_NONE = 0,
	/* activated when int coalescing time is set to a non-0 value */
	ENETC_IC_RX_MANUAL = BIT(0),
	ENETC_IC_TX_MANUAL = BIT(1),
	/* use dynamic interrupt moderation */
	ENETC_IC_RX_ADAPTIVE = BIT(2),
};

#define ENETC_RXIC_PKTTHR	min_t(u32, 256, ENETC_RX_RING_DEFAULT_SIZE / 2)
#define ENETC_TXIC_PKTTHR	min_t(u32, 128, ENETC_TX_RING_DEFAULT_SIZE / 2)

#define ENETC_TXIC_TIMETHR	enetc_usecs_to_cycles(600, ENETC_CLK)
#define ENETC4_TXIC_TIMETHR	enetc_usecs_to_cycles(500, ENETC4_CLK)

struct enetc_psfp_chain {
	struct hlist_head isit_list;
	struct hlist_head sgit_list;
	struct hlist_head rpt_list;
	spinlock_t psfp_lock; /* spinlock for the struct enetc_psfp r/w */
};

struct enetc_ndev_priv {
	struct net_device *ndev;
	struct device *dev; /* dma-mapping device */
	struct enetc_si *si;
	struct clk *ref_clk; /* RGMII/RMII reference clock */
	struct pci_dev *rcec;

	int bdr_int_num; /* number of Rx/Tx ring interrupts */
	struct enetc_int_vector *int_vector[ENETC_MAX_BDR_INT];
	u16 num_rx_rings, num_tx_rings;
	u16 rx_bd_count, tx_bd_count;

	u16 msg_enable;

	u8 preemptible_tcs;
	/* Kernel stack and XDP share the tx rings, note that shared_tx_ring
	 * cannot be set to 'true' when enetc_has_err050089 is true, because
	 * this may cause a deadlock.
	 */
	bool shared_tx_rings;

	enum enetc_active_offloads active_offloads;

	u32 speed; /* store speed for compare update pspeed */
	struct enetc_bdr **xdp_tx_ring;
	struct enetc_bdr *tx_ring[16];
	struct enetc_bdr *rx_ring[16];
	const struct enetc_bdr_resource *tx_res;
	const struct enetc_bdr_resource *rx_res;

	struct enetc_cls_rule *cls_rules;
	int max_ipf_entries;
	u32 ipt_wol_eid;

	struct ethtool_eee eee;

	union psfp_cap psfp_cap;
	struct enetc_psfp_chain psfp_chain;
	unsigned long *ist_bitmap;
	unsigned long *isct_bitmap;
	unsigned long *sgclt_used_words;

	/* Minimum number of TX queues required by the network stack */
	unsigned int min_num_stack_tx_queues;

	struct phylink *phylink;
	int ic_mode;
	u32 tx_ictt;

	struct bpf_prog *xdp_prog;

	unsigned long flags;
	int wolopts;

	struct work_struct	tx_onestep_tstamp;
	struct sk_buff_head	tx_skbs;

	/* The maximum number of BDs for fragments */
	int max_frags_bd;

	/* Serialize access to MAC Merge state between ethtool requests
	 * and link state updates
	 */
	struct mutex		mm_lock;
};

#define ENETC_CBD(R, i)	(&(((struct enetc_cbd *)((R).bd_base))[i]))

#define ENETC_CBDR_TIMEOUT	1000 /* usecs */

/* PTP driver exports */
extern int enetc_phc_index;

/* SI common */
u32 enetc_port_mac_rd(struct enetc_si *si, u32 reg);
void enetc_port_mac_wr(struct enetc_si *si, u32 reg, u32 val);
int enetc_pci_probe(struct pci_dev *pdev, const char *name, int sizeof_priv);
void enetc_pci_remove(struct pci_dev *pdev);
int enetc_alloc_msix(struct enetc_ndev_priv *priv);
void enetc_free_msix(struct enetc_ndev_priv *priv);
void enetc_get_si_caps(struct enetc_si *si);
void enetc_init_si_rings_params(struct enetc_ndev_priv *priv);
int enetc_alloc_si_resources(struct enetc_ndev_priv *priv);
void enetc_free_si_resources(struct enetc_ndev_priv *priv);
int enetc_configure_si(struct enetc_ndev_priv *priv);

int enetc_suspend(struct net_device *ndev, bool wol);
int enetc_resume(struct net_device *ndev, bool wol);
int enetc_open(struct net_device *ndev);
int enetc_close(struct net_device *ndev);
void enetc_start(struct net_device *ndev);
void enetc_stop(struct net_device *ndev);
netdev_tx_t enetc_xmit(struct sk_buff *skb, struct net_device *ndev);
struct net_device_stats *enetc_get_stats(struct net_device *ndev);
void enetc_set_features(struct net_device *ndev, netdev_features_t features);
int enetc_ioctl(struct net_device *ndev, struct ifreq *rq, int cmd);
int enetc_setup_tc_mqprio(struct net_device *ndev, void *type_data);
void enetc_reset_tc_mqprio(struct net_device *ndev);
int enetc_setup_bpf(struct net_device *ndev, struct netdev_bpf *bpf);
int enetc_xdp_xmit(struct net_device *ndev, int num_frames,
		   struct xdp_frame **frames, u32 flags);
void enetc_change_preemptible_tcs(struct enetc_ndev_priv *priv,
				  u8 preemptible_tcs);
void enetc_reset_mac_addr_filter(struct enetc_mac_filter *filter);
void enetc_add_mac_addr_ht_filter(struct enetc_mac_filter *filter,
				  const unsigned char *addr);
int enetc_vid_hash_idx(unsigned int vid);
void enetc_refresh_vlan_ht_filter(struct enetc_si *si);

/* ethtool */
void enetc_set_ethtool_ops(struct net_device *ndev);
void enetc_mm_link_state_update(struct enetc_ndev_priv *priv, bool link);
void enetc_mm_commit_preemptible_tcs(struct enetc_ndev_priv *priv);
void enetc_eee_mode_set(struct net_device *dev, bool enable);

/* control buffer descriptor ring (CBDR) */
int enetc_init_cbdr(struct enetc_si *si);
void enetc_free_cbdr(struct enetc_si *si);
int enetc_set_mac_flt_entry(struct enetc_si *si, int index,
			    char *mac_addr, int si_map);
int enetc_clear_mac_flt_entry(struct enetc_si *si, int index);
int enetc_set_fs_entry(struct enetc_si *si, struct enetc_cmd_rfse *rfse,
		       int index);
void enetc_set_rss_key(struct enetc_hw *hw, const u8 *bytes);
int enetc_get_rss_table(struct enetc_si *si, u32 *table, int count);
int enetc_set_rss_table(struct enetc_si *si, const u32 *table, int count);
int enetc_send_cmd(struct enetc_si *si, struct enetc_cbd *cbd);

static inline bool enetc_ptp_clock_is_enabled(struct enetc_si *si)
{
	return !!((IS_ENABLED(CONFIG_FSL_ENETC_PTP_CLOCK) && is_enetc_rev1(si)) ||
		  (IS_ENABLED(CONFIG_PTP_1588_CLOCK_NETC) && is_enetc_rev4(si)));
}

static inline union enetc_rx_bd *enetc_rxbd(struct enetc_bdr *rx_ring, int i)
{
	int hw_idx = i;

	if (rx_ring->ext_en)
		hw_idx = 2 * i;

	return &(((union enetc_rx_bd *)rx_ring->bd_base)[hw_idx]);
}

static inline void enetc_rxbd_next(struct enetc_bdr *rx_ring,
				   union enetc_rx_bd **old_rxbd, int *old_index)
{
	union enetc_rx_bd *new_rxbd = *old_rxbd;
	int new_index = *old_index;

	new_rxbd++;

	if (rx_ring->ext_en)
		new_rxbd++;

	if (unlikely(++new_index == rx_ring->bd_count)) {
		new_rxbd = rx_ring->bd_base;
		new_index = 0;
	}

	*old_rxbd = new_rxbd;
	*old_index = new_index;
}

static inline void *enetc_cbd_alloc_data_mem(struct enetc_si *si,
					     struct enetc_cbd *cbd,
					     int size, dma_addr_t *dma,
					     void **data_align)
{
	struct enetc_cbdr *ring = &si->cbd_ring;
	dma_addr_t dma_align;
	void *data;

	data = dma_alloc_coherent(ring->dma_dev,
				  size + ENETC_CBD_DATA_MEM_ALIGN,
				  dma, GFP_KERNEL);
	if (!data) {
		dev_err(ring->dma_dev, "CBD alloc data memory failed!\n");
		return NULL;
	}

	dma_align = ALIGN(*dma, ENETC_CBD_DATA_MEM_ALIGN);
	*data_align = PTR_ALIGN(data, ENETC_CBD_DATA_MEM_ALIGN);

	cbd->addr[0] = cpu_to_le32(lower_32_bits(dma_align));
	cbd->addr[1] = cpu_to_le32(upper_32_bits(dma_align));
	cbd->length = cpu_to_le16(size);

	return data;
}

static inline void enetc_cbd_free_data_mem(struct enetc_si *si, int size,
					   void *data, dma_addr_t *dma)
{
	struct enetc_cbdr *ring = &si->cbd_ring;

	dma_free_coherent(ring->dma_dev, size + ENETC_CBD_DATA_MEM_ALIGN,
			  data, *dma);
}

#ifdef CONFIG_FSL_ENETC_QOS
int enetc_qos_query_caps(struct net_device *ndev, void *type_data);
int enetc_setup_tc_taprio(struct net_device *ndev, void *type_data);
int enetc_setup_tc_cbs(struct net_device *ndev, void *type_data);
int enetc_setup_tc_txtime(struct net_device *ndev, void *type_data);
int enetc_setup_tc_psfp(struct net_device *ndev, void *type_data);
int enetc_psfp_init(struct enetc_ndev_priv *priv);
int enetc_psfp_clean(struct enetc_ndev_priv *priv);
int enetc4_psfp_init(struct enetc_ndev_priv *priv);
int enetc4_psfp_clean(struct enetc_ndev_priv *priv);
int enetc_set_psfp(struct net_device *ndev, bool en);

static inline void enetc_get_max_cap(struct enetc_ndev_priv *priv)
{
	struct enetc_hw *hw = &priv->si->hw;
	u32 reg;

	reg = enetc_port_rd(hw, ENETC_PSIDCAPR);
	priv->psfp_cap.max_streamid = reg & ENETC_PSIDCAPR_MSK;
	/* Port stream filter capability */
	reg = enetc_port_rd(hw, ENETC_PSFCAPR);
	priv->psfp_cap.max_psfp_filter = reg & ENETC_PSFCAPR_MSK;
	/* Port stream gate capability */
	reg = enetc_port_rd(hw, ENETC_PSGCAPR);
	priv->psfp_cap.max_psfp_gate = (reg & ENETC_PSGCAPR_SGIT_MSK);
	priv->psfp_cap.max_psfp_gatelist = (reg & ENETC_PSGCAPR_GCL_MSK) >> 16;
	/* Port flow meter capability */
	reg = enetc_port_rd(hw, ENETC_PFMCAPR);
	priv->psfp_cap.max_psfp_meter = reg & ENETC_PFMCAPR_MSK;
}

static inline void enetc4_get_psfp_caps(struct enetc_ndev_priv *priv)
{
	struct enetc_hw *hw = &priv->si->hw;
	u32 reg;

	/* Get the max number of entris of RP table */
	reg = enetc_port_rd(hw, ENETC4_RPITCAPR);
	priv->psfp_cap.ntmp.max_rpt_entries = reg & RPITCAPR_NUM_ENTRIES;
	/* Get the max number of entris of ISI and ISF table */
	reg = enetc_port_rd(hw, ENETC4_HTMCAPR);
	/* For ENETC4, HTMCAPR is shared by ISID and ISF tables */
	priv->psfp_cap.ntmp.max_isit_entries = (reg & HTMCAPR_NUM_WORDS) / 2;
	priv->psfp_cap.ntmp.max_isft_entries = (reg & HTMCAPR_NUM_WORDS) / 2;
	/* Get the max number of entris of IS table */
	reg = enetc_port_rd(hw, ENETC4_ISITCAPR);
	priv->psfp_cap.ntmp.max_ist_entries = reg & ISITCAPR_NUM_ENTRIES;
	/* Get the max number of entris of SGI table */
	reg = enetc_port_rd(hw, ENETC4_SGIITCAPR);
	priv->psfp_cap.ntmp.max_sgit_entries = reg & SGITCAPR_NUM_ENTRIES;
	/* Get the max number of entris of ISC table */
	reg = enetc_port_rd(hw, ENETC4_ISCICAPR);
	priv->psfp_cap.ntmp.max_isct_entries = reg & ISCICAPR_NUM_ENTRIES;
	/* Get the max number of words of SGCL table */
	reg = enetc_port_rd(hw, ENETC4_SGCLITCAPR);
	priv->psfp_cap.ntmp.sgcl_num_words = reg & SGCLITCAPR_NUM_WORDS;
}

static inline int enetc_psfp_enable(struct enetc_ndev_priv *priv)
{
	struct enetc_hw *hw = &priv->si->hw;
	int err;

	if (is_enetc_rev1(priv->si)) {
		enetc_get_max_cap(priv);

		err = enetc_psfp_init(priv);
		if (err)
			return err;

		enetc_wr(hw, ENETC_PPSFPMR, enetc_rd(hw, ENETC_PPSFPMR) |
			ENETC_PPSFPMR_PSFPEN | ENETC_PPSFPMR_VS |
			ENETC_PPSFPMR_PVC | ENETC_PPSFPMR_PVZC);
	} else {
		enetc4_get_psfp_caps(priv);

		err = enetc4_psfp_init(priv);
		if (err)
			return err;
	}

	return 0;
}

static inline int enetc_psfp_disable(struct enetc_ndev_priv *priv)
{
	struct enetc_hw *hw = &priv->si->hw;
	int err;

	if (is_enetc_rev1(priv->si)) {
		err = enetc_psfp_clean(priv);
		if (err)
			return err;

		enetc_wr(hw, ENETC_PPSFPMR, enetc_rd(hw, ENETC_PPSFPMR) &
			 ~ENETC_PPSFPMR_PSFPEN & ~ENETC_PPSFPMR_VS &
			 ~ENETC_PPSFPMR_PVC & ~ENETC_PPSFPMR_PVZC);
	} else {
		err = enetc4_psfp_clean(priv);
		if (err)
			return err;
	}

	memset(&priv->psfp_cap, 0, sizeof(union psfp_cap));

	return 0;
}

#else
#define enetc_qos_query_caps(ndev, type_data) -EOPNOTSUPP
#define enetc_setup_tc_taprio(ndev, type_data) -EOPNOTSUPP
#define enetc_setup_tc_cbs(ndev, type_data) -EOPNOTSUPP
#define enetc_setup_tc_txtime(ndev, type_data) -EOPNOTSUPP
#define enetc_setup_tc_psfp(ndev, type_data) -EOPNOTSUPP

#define enetc_get_max_cap(p)		\
	memset(&((p)->psfp_cap), 0, sizeof(struct psfp_cap))

static inline int enetc_psfp_enable(struct enetc_ndev_priv *priv)
{
	return -EOPNOTSUPP;
}

static inline int enetc_set_psfp(struct net_device *ndev, bool en)
{
	return 0;
}
#endif

#if IS_ENABLED(CONFIG_TSN)

void enetc_tsn_pf_init(struct net_device *netdev, struct pci_dev *pdev);
void enetc_tsn_pf_deinit(struct net_device *netdev);

#else

static inline void enetc_tsn_pf_init(struct net_device *netdev, struct pci_dev *pdev)
{
}

static inline void enetc_tsn_pf_deinit(struct net_device *netdev)
{
}

#endif

#if IS_ENABLED(CONFIG_DEBUG_FS)
void enetc_create_debugfs(struct enetc_si *si);
void enetc_remove_debugfs(struct enetc_si *si);
#else
static inline void enetc_create_debugfs(struct enetc_si *si)
{
}

static inline void enetc_remove_debugfs(struct enetc_si *si)
{
}
#endif