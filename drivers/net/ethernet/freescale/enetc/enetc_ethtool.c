// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2017-2019 NXP */

#include <linux/ethtool_netlink.h>
#include <linux/fsl/netc_prb_ierb.h>
#include <linux/fsl/ptp_netc.h>
#include <linux/net_tstamp.h>
#include <linux/module.h>
#include "enetc_pf.h"

static const u32 enetc_si_regs[] = {
	ENETC_SIMR, ENETC_SIPMAR0, ENETC_SIPMAR1, ENETC_SICBDRMR,
	ENETC_SICBDRSR,	ENETC_SICBDRBAR0, ENETC_SICBDRBAR1, ENETC_SICBDRPIR,
	ENETC_SICBDRCIR, ENETC_SICBDRLENR, ENETC_SICAPR0, ENETC_SICAPR1
};

static const u32 enetc_txbdr_regs[] = {
	ENETC_TBMR, ENETC_TBSR, ENETC_TBBAR0, ENETC_TBBAR1,
	ENETC_TBPIR, ENETC_TBCIR, ENETC_TBLENR, ENETC_TBIER, ENETC_TBICR0,
	ENETC_TBICR1
};

static const u32 enetc_rxbdr_regs[] = {
	ENETC_RBMR, ENETC_RBSR, ENETC_RBBSR, ENETC_RBCIR, ENETC_RBBAR0,
	ENETC_RBBAR1, ENETC_RBPIR, ENETC_RBLENR, ENETC_RBIER, ENETC_RBICR0,
	ENETC_RBICR1
};

static const u32 enetc_port_regs[] = {
	ENETC_PMR, ENETC_PSR, ENETC_PSIPMR, ENETC_PSIPMAR0(0),
	ENETC_PSIPMAR1(0), ENETC_PTXMBAR, ENETC_PCAPR0, ENETC_PCAPR1,
	ENETC_PSICFGR0(0), ENETC_PRFSCAPR, ENETC_PTCMSDUR(0),
	ENETC_PM0_CMD_CFG, ENETC_PM0_MAXFRM, ENETC_PM0_IF_MODE
};

static const u32 enetc4_port_regs[] = {
	ENETC4_PMR, ENETC4_PPAUONTR, ENETC4_PPAUOFFTR, ENETC4_PSIPMMR,
	ENETC4_PSIPVMR, ENETC4_PSIVLANFMR, ENETC4_PCAPR, ENETC4_PMCAPR,
	ENETC4_PCR, ENETC4_PMAR0, ENETC4_PMAR1, ENETC4_PSR,
	ENETC4_PM_CMD_CFG(0), ENETC4_PM_PAUSE_QUANTA(0),
	ENETC4_PM_PAUSE_THRESH(0), ENETC4_PM_MAXFRM(0), ENETC4_PM_IF_MODE(0)
};

static const u32 enetc_port_mm_regs[] = {
	ENETC_MMCSR, ENETC_PFPMR, ENETC_PTCFPR(0), ENETC_PTCFPR(1),
	ENETC_PTCFPR(2), ENETC_PTCFPR(3), ENETC_PTCFPR(4), ENETC_PTCFPR(5),
	ENETC_PTCFPR(6), ENETC_PTCFPR(7),
};

static const u32 enetc4_port_mm_regs[] = {
	ENETC4_MMCSR, ENETC4_PFPCR,
};

static int enetc_get_reglen(struct net_device *ndev)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_si *si = priv->si;
	int len;

	len = ARRAY_SIZE(enetc_si_regs);
	len += ARRAY_SIZE(enetc_txbdr_regs) * priv->num_tx_rings;
	len += ARRAY_SIZE(enetc_rxbdr_regs) * priv->num_rx_rings;

	if (enetc_si_is_pf(si)) {
		if (is_enetc_rev1(si)) {
			len += ARRAY_SIZE(enetc_port_regs);
			if (!!(si->hw_features & ENETC_SI_F_QBU))
				len += ARRAY_SIZE(enetc_port_mm_regs);
		} else {
			len += ARRAY_SIZE(enetc4_port_regs);
			if (!!(si->hw_features & ENETC_SI_F_QBU))
				len += ARRAY_SIZE(enetc4_port_mm_regs);
		}
	}

	len *= sizeof(u32) * 2; /* store 2 entries per reg: addr and value */

	return len;
}

static void enetc_get_regs(struct net_device *ndev, struct ethtool_regs *regs,
			   void *regbuf)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_si *si = priv->si;
	struct enetc_hw *hw = &si->hw;
	u32 *buf = (u32 *)regbuf;
	int i, j;
	u32 addr;

	for (i = 0; i < ARRAY_SIZE(enetc_si_regs); i++) {
		*buf++ = enetc_si_regs[i];
		*buf++ = enetc_rd(hw, enetc_si_regs[i]);
	}

	for (i = 0; i < priv->num_tx_rings; i++) {
		for (j = 0; j < ARRAY_SIZE(enetc_txbdr_regs); j++) {
			addr = ENETC_BDR(TX, i, enetc_txbdr_regs[j]);

			*buf++ = addr;
			*buf++ = enetc_rd(hw, addr);
		}
	}

	for (i = 0; i < priv->num_rx_rings; i++) {
		for (j = 0; j < ARRAY_SIZE(enetc_rxbdr_regs); j++) {
			addr = ENETC_BDR(RX, i, enetc_rxbdr_regs[j]);

			*buf++ = addr;
			*buf++ = enetc_rd(hw, addr);
		}
	}

	if (!enetc_si_is_pf(si))
		return;

	if (is_enetc_rev1(si)) {
		for (i = 0; i < ARRAY_SIZE(enetc_port_regs); i++) {
			*buf++ = enetc_port_regs[i];
			*buf++ = enetc_port_rd(hw, enetc_port_regs[i]);
		}

		if (si->hw_features & ENETC_SI_F_QBU) {
			for (i = 0; i < ARRAY_SIZE(enetc_port_mm_regs); i++) {
				*buf++ = enetc_port_mm_regs[i];
				*buf++ = enetc_port_rd(hw, enetc_port_mm_regs[i]);
			}
		}
	} else {
		for (i = 0; i < ARRAY_SIZE(enetc4_port_regs); i++) {
			*buf++ = enetc4_port_regs[i];
			*buf++ = enetc_port_rd(hw, enetc4_port_regs[i]);
		}

		if (si->hw_features & ENETC_SI_F_QBU) {
			for (i = 0; i < ARRAY_SIZE(enetc4_port_mm_regs); i++) {
				*buf++ = enetc4_port_mm_regs[i];
				*buf++ = enetc_port_rd(hw, enetc4_port_mm_regs[i]);
			}
		}
	}

}

static const struct {
	int reg;
	char name[ETH_GSTRING_LEN];
} enetc_si_counters[] =  {
	{ ENETC_SIROCT, "SI rx octets" },
	{ ENETC_SIRFRM, "SI rx frames" },
	{ ENETC_SIRUCA, "SI rx u-cast frames" },
	{ ENETC_SIRMCA, "SI rx m-cast frames" },
	{ ENETC_SITOCT, "SI tx octets" },
	{ ENETC_SITFRM, "SI tx frames" },
	{ ENETC_SITUCA, "SI tx u-cast frames" },
	{ ENETC_SITMCA, "SI tx m-cast frames" },
};

static const struct {
	int reg;
	char name[ETH_GSTRING_LEN];
} enetc4_si_extend_counters[] =  {
	{ ENETC4_SITDFCR, "SI tx discarded frames" },
};

static const struct {
	int reg;
	char name[ETH_GSTRING_LEN];
} enetc_port_counters[] = {
	{ ENETC_PM_REOCT(0),	"MAC rx ethernet octets" },
	{ ENETC_PM_RALN(0),	"MAC rx alignment errors" },
	{ ENETC_PM_RXPF(0),	"MAC rx valid pause frames" },
	{ ENETC_PM_RFRM(0),	"MAC rx valid frames" },
	{ ENETC_PM_RFCS(0),	"MAC rx fcs errors" },
	{ ENETC_PM_RVLAN(0),	"MAC rx VLAN frames" },
	{ ENETC_PM_RERR(0),	"MAC rx frame errors" },
	{ ENETC_PM_RUCA(0),	"MAC rx unicast frames" },
	{ ENETC_PM_RMCA(0),	"MAC rx multicast frames" },
	{ ENETC_PM_RBCA(0),	"MAC rx broadcast frames" },
	{ ENETC_PM_RDRP(0),	"MAC rx dropped packets" },
	{ ENETC_PM_RPKT(0),	"MAC rx packets" },
	{ ENETC_PM_RUND(0),	"MAC rx undersized packets" },
	{ ENETC_PM_R64(0),	"MAC rx 64 byte packets" },
	{ ENETC_PM_R127(0),	"MAC rx 65-127 byte packets" },
	{ ENETC_PM_R255(0),	"MAC rx 128-255 byte packets" },
	{ ENETC_PM_R511(0),	"MAC rx 256-511 byte packets" },
	{ ENETC_PM_R1023(0),	"MAC rx 512-1023 byte packets" },
	{ ENETC_PM_R1522(0),	"MAC rx 1024-1522 byte packets" },
	{ ENETC_PM_R1523X(0),	"MAC rx 1523 to max-octet packets" },
	{ ENETC_PM_ROVR(0),	"MAC rx oversized packets" },
	{ ENETC_PM_RJBR(0),	"MAC rx jabber packets" },
	{ ENETC_PM_RFRG(0),	"MAC rx fragment packets" },
	{ ENETC_PM_RCNP(0),	"MAC rx control packets" },
	{ ENETC_PM_RDRNTP(0),	"MAC rx fifo drop" },
	{ ENETC_PM_TEOCT(0),	"MAC tx ethernet octets" },
	{ ENETC_PM_TOCT(0),	"MAC tx octets" },
	{ ENETC_PM_TCRSE(0),	"MAC tx carrier sense errors" },
	{ ENETC_PM_TXPF(0),	"MAC tx valid pause frames" },
	{ ENETC_PM_TFRM(0),	"MAC tx frames" },
	{ ENETC_PM_TFCS(0),	"MAC tx fcs errors" },
	{ ENETC_PM_TVLAN(0),	"MAC tx VLAN frames" },
	{ ENETC_PM_TERR(0),	"MAC tx frame errors" },
	{ ENETC_PM_TUCA(0),	"MAC tx unicast frames" },
	{ ENETC_PM_TMCA(0),	"MAC tx multicast frames" },
	{ ENETC_PM_TBCA(0),	"MAC tx broadcast frames" },
	{ ENETC_PM_TPKT(0),	"MAC tx packets" },
	{ ENETC_PM_TUND(0),	"MAC tx undersized packets" },
	{ ENETC_PM_T64(0),	"MAC tx 64 byte packets" },
	{ ENETC_PM_T127(0),	"MAC tx 65-127 byte packets" },
	{ ENETC_PM_T255(0),	"MAC tx 128-255 byte packets" },
	{ ENETC_PM_T511(0),	"MAC tx 256-511 byte packets" },
	{ ENETC_PM_T1023(0),	"MAC tx 512-1023 byte packets" },
	{ ENETC_PM_T1522(0),	"MAC tx 1024-1522 byte packets" },
	{ ENETC_PM_T1523X(0),	"MAC tx 1523 to max-octet packets" },
	{ ENETC_PM_TCNP(0),	"MAC tx control packets" },
	{ ENETC_PM_TDFR(0),	"MAC tx deferred packets" },
	{ ENETC_PM_TMCOL(0),	"MAC tx multiple collisions" },
	{ ENETC_PM_TSCOL(0),	"MAC tx single collisions" },
	{ ENETC_PM_TLCOL(0),	"MAC tx late collisions" },
	{ ENETC_PM_TECOL(0),	"MAC tx excessive collisions" },
	{ ENETC_UFDMF,		"SI MAC nomatch u-cast discards" },
	{ ENETC_MFDMF,		"SI MAC nomatch m-cast discards" },
	{ ENETC_PBFDSIR,	"SI MAC nomatch b-cast discards" },
	{ ENETC_PUFDVFR,	"SI VLAN nomatch u-cast discards" },
	{ ENETC_PMFDVFR,	"SI VLAN nomatch m-cast discards" },
	{ ENETC_PBFDVFR,	"SI VLAN nomatch b-cast discards" },
	{ ENETC_PFDMSAPR,	"SI pruning discarded frames" },
	{ ENETC_PICDR(0),	"ICM DR0 discarded frames" },
	{ ENETC_PICDR(1),	"ICM DR1 discarded frames" },
	{ ENETC_PICDR(2),	"ICM DR2 discarded frames" },
	{ ENETC_PICDR(3),	"ICM DR3 discarded frames" },
};

static const struct {
	int reg;
	char name[ETH_GSTRING_LEN];
} enetc4_port_counters[] = {
	{ ENETC4_PICDRDCR(0),	"ICM DR0 discarded frames" },
	{ ENETC4_PICDRDCR(1),	"ICM DR1 discarded frames" },
	{ ENETC4_PICDRDCR(2),	"ICM DR2 discarded frames" },
	{ ENETC4_PICDRDCR(3),	"ICM DR3 discarded frames" },
	{ ENETC4_PUFDMFR,	"MAC filter discarded unicast" },
	{ ENETC4_PMFDMFR,	"MAC filter discarded multicast" },
	{ ENETC4_PBFDSIR,	"MAC filter discarded broadcast" },
	{ ENETC4_PFDMSAPR,	"MAC SA pruning discarded frames" },
	{ ENETC4_PUFDVFR,	"VLAN filter discarded unicast" },
	{ ENETC4_PMFDVFR,	"VLAN filter discarded multicast" },
	{ ENETC4_PBFDVFR,	"VLAN filter discarded broadcast" },
	{ ENETC4_PRXDCR,	"MAC rx discarded frames" },
	{ ENETC4_PM_REOCT(0),	"MAC rx ethernet octets" },
	{ ENETC4_PM_ROCT(0),	"MAC rx octets" },
	{ ENETC4_PM_RXPF(0),	"MAC rx valid pause frames" },
	{ ENETC4_PM_RFRM(0),	"MAC rx valid frames" },
	{ ENETC4_PM_RFCS(0),	"MAC rx fcs errors" },
	{ ENETC4_PM_RVLAN(0),	"MAC rx VLAN frames" },
	{ ENETC4_PM_RERR(0),	"MAC rx frame errors" },
	{ ENETC4_PM_RUCA(0),	"MAC rx unicast frames" },
	{ ENETC4_PM_RMCA(0),	"MAC rx multicast frames" },
	{ ENETC4_PM_RBCA(0),	"MAC rx broadcast frames" },
	{ ENETC4_PM_RDRP(0),	"MAC rx dropped packets" },
	{ ENETC4_PM_RPKT(0),	"MAC rx packets" },
	{ ENETC4_PM_RUND(0),	"MAC rx undersized packets" },
	{ ENETC4_PM_R64(0),	"MAC rx 64 byte packets" },
	{ ENETC4_PM_R127(0),	"MAC rx 65-127 byte packets" },
	{ ENETC4_PM_R255(0),	"MAC rx 128-255 byte packets" },
	{ ENETC4_PM_R511(0),	"MAC rx 256-511 byte packets" },
	{ ENETC4_PM_R1023(0),	"MAC rx 512-1023 byte packets" },
	{ ENETC4_PM_R1522(0),	"MAC rx 1024-1522 byte packets" },
	{ ENETC4_PM_R1523X(0),	"MAC rx 1523 to max-octet packets" },
	{ ENETC4_PM_ROVR(0),	"MAC rx oversized packets" },
	{ ENETC4_PM_RJBR(0),	"MAC rx jabber packets" },
	{ ENETC4_PM_RFRG(0),	"MAC rx fragment packets" },
	{ ENETC4_PM_RCNP(0),	"MAC rx control packets" },
	{ ENETC4_PM_RDRNTP(0),	"MAC rx fifo drop" },
	{ ENETC4_PM_TEOCT(0),	"MAC tx ethernet octets" },
	{ ENETC4_PM_TOCT(0),	"MAC tx octets" },
	{ ENETC4_PM_TXPF(0),	"MAC tx valid pause frames" },
	{ ENETC4_PM_TFRM(0),	"MAC tx frames" },
	{ ENETC4_PM_TFCS(0),	"MAC tx fcs errors" },
	{ ENETC4_PM_TVLAN(0),	"MAC tx VLAN frames" },
	{ ENETC4_PM_TERR(0),	"MAC tx frame errors" },
	{ ENETC4_PM_TUCA(0),	"MAC tx unicast frames" },
	{ ENETC4_PM_TMCA(0),	"MAC tx multicast frames" },
	{ ENETC4_PM_TBCA(0),	"MAC tx broadcast frames" },
	{ ENETC4_PM_TPKT(0),	"MAC tx packets" },
	{ ENETC4_PM_TUND(0),	"MAC tx undersized packets" },
	{ ENETC4_PM_T64(0),	"MAC tx 64 byte packets" },
	{ ENETC4_PM_T127(0),	"MAC tx 65-127 byte packets" },
	{ ENETC4_PM_T255(0),	"MAC tx 128-255 byte packets" },
	{ ENETC4_PM_T511(0),	"MAC tx 256-511 byte packets" },
	{ ENETC4_PM_T1023(0),	"MAC tx 512-1023 byte packets" },
	{ ENETC4_PM_T1522(0),	"MAC tx 1024-1522 byte packets" },
	{ ENETC4_PM_T1523X(0),	"MAC tx 1523 to max-octet packets" },
	{ ENETC4_PM_TCNP(0),	"MAC tx control packets" },
	{ ENETC4_PM_TDFR(0),	"MAC tx deferred packets" },
	{ ENETC4_PM_TMCOL(0),	"MAC tx multiple collisions" },
	{ ENETC4_PM_TSCOL(0),	"MAC tx single collisions" },
	{ ENETC4_PM_TLCOL(0),	"MAC tx late collisions" },
	{ ENETC4_PM_TECOL(0),	"MAC tx excessive collisions" },
};

static const char rx_ring_stats[][ETH_GSTRING_LEN] = {
	"Rx ring %2d discarded frames",
	"Rx ring %2d frames",
	"Rx ring %2d alloc errors",
	"Rx ring %2d XDP drops",
	"Rx ring %2d recycles",
	"Rx ring %2d recycle failures",
	"Rx ring %2d redirects",
	"Rx ring %2d redirect failures",
};

static const char tx_ring_stats[][ETH_GSTRING_LEN] = {
	"Tx ring %2d frames",
	"Tx ring %2d XDP frames",
	"Tx ring %2d XDP drops",
	"Tx window drop %2d frames",
};

static int enetc_get_sset_count(struct net_device *ndev, int sset)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_si *si = priv->si;
	int len;

	if (sset != ETH_SS_STATS)
		return -EOPNOTSUPP;

	len = ARRAY_SIZE(enetc_si_counters) +
	      ARRAY_SIZE(tx_ring_stats) * priv->num_tx_rings +
	      ARRAY_SIZE(rx_ring_stats) * priv->num_rx_rings;

	if (is_enetc_rev4(si))
		len += ARRAY_SIZE(enetc4_si_extend_counters);

	if (!enetc_si_is_pf(priv->si))
		return len;

	len += ARRAY_SIZE(enetc_port_counters);

	return len;
}

static void enetc_get_strings(struct net_device *ndev, u32 stringset, u8 *data)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_si *si = priv->si;
	u8 *p = data;
	int i, j;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < ARRAY_SIZE(enetc_si_counters); i++) {
			strscpy(p, enetc_si_counters[i].name, ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}

		if (is_enetc_rev4(si))
			for (i = 0; i < ARRAY_SIZE(enetc4_si_extend_counters); i++) {
				strscpy(p, enetc4_si_extend_counters[i].name, ETH_GSTRING_LEN);
				p += ETH_GSTRING_LEN;
			}

		for (i = 0; i < priv->num_tx_rings; i++) {
			for (j = 0; j < ARRAY_SIZE(tx_ring_stats); j++) {
				snprintf(p, ETH_GSTRING_LEN, tx_ring_stats[j],
					 i);
				p += ETH_GSTRING_LEN;
			}
		}
		for (i = 0; i < priv->num_rx_rings; i++) {
			for (j = 0; j < ARRAY_SIZE(rx_ring_stats); j++) {
				snprintf(p, ETH_GSTRING_LEN, rx_ring_stats[j],
					 i);
				p += ETH_GSTRING_LEN;
			}
		}

		if (!enetc_si_is_pf(si))
			break;

		if (is_enetc_rev1(si)) {
			for (i = 0; i < ARRAY_SIZE(enetc_port_counters); i++) {
				strscpy(p, enetc_port_counters[i].name,
					ETH_GSTRING_LEN);
				p += ETH_GSTRING_LEN;
			}
		} else {
			for (i = 0; i < ARRAY_SIZE(enetc4_port_counters); i++) {
				strscpy(p, enetc4_port_counters[i].name,
					ETH_GSTRING_LEN);
				p += ETH_GSTRING_LEN;
			}
		}
		break;
	}
}

static void enetc_get_ethtool_stats(struct net_device *ndev,
				    struct ethtool_stats *stats, u64 *data)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_si *si = priv->si;
	struct enetc_hw *hw = &si->hw;
	int i, o = 0;

	for (i = 0; i < ARRAY_SIZE(enetc_si_counters); i++)
		data[o++] = enetc_rd64(hw, enetc_si_counters[i].reg);

	if (is_enetc_rev4(priv->si)) {
		for (i = 0; i < ARRAY_SIZE(enetc4_si_extend_counters); i++)
			data[o++] = enetc_rd(hw, enetc4_si_extend_counters[i].reg);
	}

	for (i = 0; i < priv->num_tx_rings; i++) {
		data[o++] = priv->tx_ring[i]->stats.packets;
		data[o++] = priv->tx_ring[i]->stats.xdp_tx;
		data[o++] = priv->tx_ring[i]->stats.xdp_tx_drops;
		data[o++] = priv->tx_ring[i]->stats.win_drop;
	}

	for (i = 0; i < priv->num_rx_rings; i++) {
		data[o++] = enetc_rd(hw, ENETC_RBDCR(i));
		data[o++] = priv->rx_ring[i]->stats.packets;
		data[o++] = priv->rx_ring[i]->stats.rx_alloc_errs;
		data[o++] = priv->rx_ring[i]->stats.xdp_drops;
		data[o++] = priv->rx_ring[i]->stats.recycles;
		data[o++] = priv->rx_ring[i]->stats.recycle_failures;
		data[o++] = priv->rx_ring[i]->stats.xdp_redirect;
		data[o++] = priv->rx_ring[i]->stats.xdp_redirect_failures;
	}

	if (!enetc_si_is_pf(si))
		return;

	if (is_enetc_rev1(si)) {
		for (i = 0; i < ARRAY_SIZE(enetc_port_counters); i++)
			data[o++] = enetc_port_rd(hw, enetc_port_counters[i].reg);
	} else {
		for (i = 0; i < ARRAY_SIZE(enetc4_port_counters); i++)
			data[o++] = enetc_port_rd(hw, enetc4_port_counters[i].reg);
	}
}

static void enetc_pause_stats(struct enetc_si *si, int mac,
			      struct ethtool_pause_stats *pause_stats)
{
	struct enetc_hw *hw = &si->hw;

	if (is_enetc_rev1(si)) {
		pause_stats->tx_pause_frames = enetc_port_rd(hw, ENETC_PM_TXPF(mac));
		pause_stats->rx_pause_frames = enetc_port_rd(hw, ENETC_PM_RXPF(mac));
	} else {
		pause_stats->tx_pause_frames = enetc_port_rd(hw, ENETC4_PM_TXPF(mac));
		pause_stats->rx_pause_frames = enetc_port_rd(hw, ENETC4_PM_RXPF(mac));
	}
}

static void enetc_get_pause_stats(struct net_device *ndev,
				  struct ethtool_pause_stats *pause_stats)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_si *si = priv->si;

	switch (pause_stats->src) {
	case ETHTOOL_MAC_STATS_SRC_EMAC:
		enetc_pause_stats(si, 0, pause_stats);
		break;
	case ETHTOOL_MAC_STATS_SRC_PMAC:
		if (si->hw_features & ENETC_SI_F_QBU)
			enetc_pause_stats(si, 1, pause_stats);
		break;
	case ETHTOOL_MAC_STATS_SRC_AGGREGATE:
		ethtool_aggregate_pause_stats(ndev, pause_stats);
		break;
	}
}

static void enetc_mac_stats(struct enetc_si *si, int mac,
			    struct ethtool_eth_mac_stats *s)
{
	struct enetc_hw *hw = &si->hw;

	if (is_enetc_rev1(si)) {
		s->FramesTransmittedOK = enetc_port_rd(hw, ENETC_PM_TFRM(mac));
		s->SingleCollisionFrames = enetc_port_rd(hw, ENETC_PM_TSCOL(mac));
		s->MultipleCollisionFrames = enetc_port_rd(hw, ENETC_PM_TMCOL(mac));
		s->FramesReceivedOK = enetc_port_rd(hw, ENETC_PM_RFRM(mac));
		s->FrameCheckSequenceErrors = enetc_port_rd(hw, ENETC_PM_RFCS(mac));
		s->AlignmentErrors = enetc_port_rd(hw, ENETC_PM_RALN(mac));
		s->OctetsTransmittedOK = enetc_port_rd(hw, ENETC_PM_TEOCT(mac));
		s->FramesWithDeferredXmissions = enetc_port_rd(hw, ENETC_PM_TDFR(mac));
		s->LateCollisions = enetc_port_rd(hw, ENETC_PM_TLCOL(mac));
		s->FramesAbortedDueToXSColls = enetc_port_rd(hw, ENETC_PM_TECOL(mac));
		s->FramesLostDueToIntMACXmitError = enetc_port_rd(hw, ENETC_PM_TERR(mac));
		s->CarrierSenseErrors = enetc_port_rd(hw, ENETC_PM_TCRSE(mac));
		s->OctetsReceivedOK = enetc_port_rd(hw, ENETC_PM_REOCT(mac));
		s->FramesLostDueToIntMACRcvError = enetc_port_rd(hw, ENETC_PM_RDRNTP(mac));
		s->MulticastFramesXmittedOK = enetc_port_rd(hw, ENETC_PM_TMCA(mac));
		s->BroadcastFramesXmittedOK = enetc_port_rd(hw, ENETC_PM_TBCA(mac));
		s->MulticastFramesReceivedOK = enetc_port_rd(hw, ENETC_PM_RMCA(mac));
		s->BroadcastFramesReceivedOK = enetc_port_rd(hw, ENETC_PM_RBCA(mac));
	} else {
		s->FramesTransmittedOK = enetc_port_rd(hw, ENETC4_PM_TFRM(mac));
		s->SingleCollisionFrames = enetc_port_rd(hw, ENETC4_PM_TSCOL(mac));
		s->MultipleCollisionFrames = enetc_port_rd(hw, ENETC4_PM_TMCOL(mac));
		s->FramesReceivedOK = enetc_port_rd(hw, ENETC4_PM_RFRM(mac));
		s->FrameCheckSequenceErrors = enetc_port_rd(hw, ENETC4_PM_RFCS(mac));
		s->AlignmentErrors = enetc_port_rd(hw, ENETC4_PM_RALN(mac));
		s->OctetsTransmittedOK = enetc_port_rd(hw, ENETC4_PM_TEOCT(mac));
		s->FramesWithDeferredXmissions = enetc_port_rd(hw, ENETC4_PM_TDFR(mac));
		s->LateCollisions = enetc_port_rd(hw, ENETC4_PM_TLCOL(mac));
		s->FramesAbortedDueToXSColls = enetc_port_rd(hw, ENETC4_PM_TECOL(mac));
		s->FramesLostDueToIntMACXmitError = enetc_port_rd(hw, ENETC4_PM_TERR(mac));
		s->OctetsReceivedOK = enetc_port_rd(hw, ENETC4_PM_REOCT(mac));
		s->FramesLostDueToIntMACRcvError = enetc_port_rd(hw, ENETC4_PM_RDRNTP(mac));
		s->MulticastFramesXmittedOK = enetc_port_rd(hw, ENETC4_PM_TMCA(mac));
		s->BroadcastFramesXmittedOK = enetc_port_rd(hw, ENETC4_PM_TBCA(mac));
		s->MulticastFramesReceivedOK = enetc_port_rd(hw, ENETC4_PM_RMCA(mac));
		s->BroadcastFramesReceivedOK = enetc_port_rd(hw, ENETC4_PM_RBCA(mac));
	}
}

static void enetc_ctrl_stats(struct enetc_si *si, int mac,
			     struct ethtool_eth_ctrl_stats *s)
{
	struct enetc_hw *hw = &si->hw;

	if (is_enetc_rev1(si)) {
		s->MACControlFramesTransmitted = enetc_port_rd(hw, ENETC_PM_TCNP(mac));
		s->MACControlFramesReceived = enetc_port_rd(hw, ENETC_PM_RCNP(mac));
	} else {
		s->MACControlFramesTransmitted = enetc_port_rd(hw, ENETC4_PM_TCNP(mac));
		s->MACControlFramesReceived = enetc_port_rd(hw, ENETC4_PM_RCNP(mac));
	}
}

static const struct ethtool_rmon_hist_range enetc_rmon_ranges[] = {
	{   64,   64 },
	{   65,  127 },
	{  128,  255 },
	{  256,  511 },
	{  512, 1023 },
	{ 1024, 1522 },
	{ 1523, ENETC_MAC_MAXFRM_SIZE },
	{},
};

static void enetc_rmon_stats(struct enetc_si *si, int mac,
			     struct ethtool_rmon_stats *s)
{
	struct enetc_hw *hw = &si->hw;

	if (is_enetc_rev1(si)) {
		s->undersize_pkts = enetc_port_rd(hw, ENETC_PM_RUND(mac));
		s->oversize_pkts = enetc_port_rd(hw, ENETC_PM_ROVR(mac));
		s->fragments = enetc_port_rd(hw, ENETC_PM_RFRG(mac));
		s->jabbers = enetc_port_rd(hw, ENETC_PM_RJBR(mac));

		s->hist[0] = enetc_port_rd(hw, ENETC_PM_R64(mac));
		s->hist[1] = enetc_port_rd(hw, ENETC_PM_R127(mac));
		s->hist[2] = enetc_port_rd(hw, ENETC_PM_R255(mac));
		s->hist[3] = enetc_port_rd(hw, ENETC_PM_R511(mac));
		s->hist[4] = enetc_port_rd(hw, ENETC_PM_R1023(mac));
		s->hist[5] = enetc_port_rd(hw, ENETC_PM_R1522(mac));
		s->hist[6] = enetc_port_rd(hw, ENETC_PM_R1523X(mac));

		s->hist_tx[0] = enetc_port_rd(hw, ENETC_PM_T64(mac));
		s->hist_tx[1] = enetc_port_rd(hw, ENETC_PM_T127(mac));
		s->hist_tx[2] = enetc_port_rd(hw, ENETC_PM_T255(mac));
		s->hist_tx[3] = enetc_port_rd(hw, ENETC_PM_T511(mac));
		s->hist_tx[4] = enetc_port_rd(hw, ENETC_PM_T1023(mac));
		s->hist_tx[5] = enetc_port_rd(hw, ENETC_PM_T1522(mac));
		s->hist_tx[6] = enetc_port_rd(hw, ENETC_PM_T1523X(mac));
	} else {
		s->undersize_pkts = enetc_port_rd(hw, ENETC4_PM_RUND(mac));
		s->oversize_pkts = enetc_port_rd(hw, ENETC4_PM_ROVR(mac));
		s->fragments = enetc_port_rd(hw, ENETC4_PM_RFRG(mac));
		s->jabbers = enetc_port_rd(hw, ENETC4_PM_RJBR(mac));

		s->hist[0] = enetc_port_rd(hw, ENETC4_PM_R64(mac));
		s->hist[1] = enetc_port_rd(hw, ENETC4_PM_R127(mac));
		s->hist[2] = enetc_port_rd(hw, ENETC4_PM_R255(mac));
		s->hist[3] = enetc_port_rd(hw, ENETC4_PM_R511(mac));
		s->hist[4] = enetc_port_rd(hw, ENETC4_PM_R1023(mac));
		s->hist[5] = enetc_port_rd(hw, ENETC4_PM_R1522(mac));
		s->hist[6] = enetc_port_rd(hw, ENETC4_PM_R1523X(mac));

		s->hist_tx[0] = enetc_port_rd(hw, ENETC4_PM_T64(mac));
		s->hist_tx[1] = enetc_port_rd(hw, ENETC4_PM_T127(mac));
		s->hist_tx[2] = enetc_port_rd(hw, ENETC4_PM_T255(mac));
		s->hist_tx[3] = enetc_port_rd(hw, ENETC4_PM_T511(mac));
		s->hist_tx[4] = enetc_port_rd(hw, ENETC4_PM_T1023(mac));
		s->hist_tx[5] = enetc_port_rd(hw, ENETC4_PM_T1522(mac));
		s->hist_tx[6] = enetc_port_rd(hw, ENETC4_PM_T1523X(mac));
	}
}

static void enetc_get_eth_mac_stats(struct net_device *ndev,
				    struct ethtool_eth_mac_stats *mac_stats)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_si *si = priv->si;

	switch (mac_stats->src) {
	case ETHTOOL_MAC_STATS_SRC_EMAC:
		enetc_mac_stats(si, 0, mac_stats);
		break;
	case ETHTOOL_MAC_STATS_SRC_PMAC:
		if (si->hw_features & ENETC_SI_F_QBU)
			enetc_mac_stats(si, 1, mac_stats);
		break;
	case ETHTOOL_MAC_STATS_SRC_AGGREGATE:
		ethtool_aggregate_mac_stats(ndev, mac_stats);
		break;
	}
}

static void enetc_get_eth_ctrl_stats(struct net_device *ndev,
				     struct ethtool_eth_ctrl_stats *ctrl_stats)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_si *si = priv->si;

	switch (ctrl_stats->src) {
	case ETHTOOL_MAC_STATS_SRC_EMAC:
		enetc_ctrl_stats(si, 0, ctrl_stats);
		break;
	case ETHTOOL_MAC_STATS_SRC_PMAC:
		if (si->hw_features & ENETC_SI_F_QBU)
			enetc_ctrl_stats(si, 1, ctrl_stats);
		break;
	case ETHTOOL_MAC_STATS_SRC_AGGREGATE:
		ethtool_aggregate_ctrl_stats(ndev, ctrl_stats);
		break;
	}
}

static void enetc_get_rmon_stats(struct net_device *ndev,
				 struct ethtool_rmon_stats *rmon_stats,
				 const struct ethtool_rmon_hist_range **ranges)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_si *si = priv->si;

	*ranges = enetc_rmon_ranges;

	switch (rmon_stats->src) {
	case ETHTOOL_MAC_STATS_SRC_EMAC:
		enetc_rmon_stats(si, 0, rmon_stats);
		break;
	case ETHTOOL_MAC_STATS_SRC_PMAC:
		if (si->hw_features & ENETC_SI_F_QBU)
			enetc_rmon_stats(si, 1, rmon_stats);
		break;
	case ETHTOOL_MAC_STATS_SRC_AGGREGATE:
		ethtool_aggregate_rmon_stats(ndev, rmon_stats);
		break;
	}
}

#define ENETC_RSSHASH_L3 (RXH_L2DA | RXH_VLAN | RXH_L3_PROTO | RXH_IP_SRC | \
			  RXH_IP_DST)
#define ENETC_RSSHASH_L4 (ENETC_RSSHASH_L3 | RXH_L4_B_0_1 | RXH_L4_B_2_3)
static int enetc_get_rsshash(struct ethtool_rxnfc *rxnfc)
{
	static const u32 rsshash[] = {
			[TCP_V4_FLOW]    = ENETC_RSSHASH_L4,
			[UDP_V4_FLOW]    = ENETC_RSSHASH_L4,
			[SCTP_V4_FLOW]   = ENETC_RSSHASH_L4,
			[AH_ESP_V4_FLOW] = ENETC_RSSHASH_L3,
			[IPV4_FLOW]      = ENETC_RSSHASH_L3,
			[TCP_V6_FLOW]    = ENETC_RSSHASH_L4,
			[UDP_V6_FLOW]    = ENETC_RSSHASH_L4,
			[SCTP_V6_FLOW]   = ENETC_RSSHASH_L4,
			[AH_ESP_V6_FLOW] = ENETC_RSSHASH_L3,
			[IPV6_FLOW]      = ENETC_RSSHASH_L3,
			[ETHER_FLOW]     = 0,
	};

	if (rxnfc->flow_type >= ARRAY_SIZE(rsshash))
		return -EINVAL;

	rxnfc->data = rsshash[rxnfc->flow_type];

	return 0;
}

/* current HW spec does byte reversal on everything including MAC addresses */
static void ether_addr_copy_swap(u8 *dst, const u8 *src)
{
	int i;

	for (i = 0; i < ETH_ALEN; i++)
		dst[i] = src[ETH_ALEN - i - 1];
}

static int enetc_set_cls_entry(struct enetc_si *si,
			       struct ethtool_rx_flow_spec *fs, bool en)
{
	struct ethtool_tcpip4_spec *l4ip4_h, *l4ip4_m;
	struct ethtool_usrip4_spec *l3ip4_h, *l3ip4_m;
	struct ethhdr *eth_h, *eth_m;
	struct enetc_cmd_rfse rfse = { {0} };

	if (!en)
		goto done;

	switch (fs->flow_type & 0xff) {
	case TCP_V4_FLOW:
		l4ip4_h = &fs->h_u.tcp_ip4_spec;
		l4ip4_m = &fs->m_u.tcp_ip4_spec;
		goto l4ip4;
	case UDP_V4_FLOW:
		l4ip4_h = &fs->h_u.udp_ip4_spec;
		l4ip4_m = &fs->m_u.udp_ip4_spec;
		goto l4ip4;
	case SCTP_V4_FLOW:
		l4ip4_h = &fs->h_u.sctp_ip4_spec;
		l4ip4_m = &fs->m_u.sctp_ip4_spec;
l4ip4:
		rfse.sip_h[0] = l4ip4_h->ip4src;
		rfse.sip_m[0] = l4ip4_m->ip4src;
		rfse.dip_h[0] = l4ip4_h->ip4dst;
		rfse.dip_m[0] = l4ip4_m->ip4dst;
		rfse.sport_h = ntohs(l4ip4_h->psrc);
		rfse.sport_m = ntohs(l4ip4_m->psrc);
		rfse.dport_h = ntohs(l4ip4_h->pdst);
		rfse.dport_m = ntohs(l4ip4_m->pdst);
		if (l4ip4_m->tos)
			netdev_warn(si->ndev, "ToS field is not supported and was ignored\n");
		rfse.ethtype_h = ETH_P_IP; /* IPv4 */
		rfse.ethtype_m = 0xffff;
		break;
	case IP_USER_FLOW:
		l3ip4_h = &fs->h_u.usr_ip4_spec;
		l3ip4_m = &fs->m_u.usr_ip4_spec;

		rfse.sip_h[0] = l3ip4_h->ip4src;
		rfse.sip_m[0] = l3ip4_m->ip4src;
		rfse.dip_h[0] = l3ip4_h->ip4dst;
		rfse.dip_m[0] = l3ip4_m->ip4dst;
		if (l3ip4_m->tos)
			netdev_warn(si->ndev, "ToS field is not supported and was ignored\n");
		rfse.ethtype_h = ETH_P_IP; /* IPv4 */
		rfse.ethtype_m = 0xffff;
		break;
	case ETHER_FLOW:
		eth_h = &fs->h_u.ether_spec;
		eth_m = &fs->m_u.ether_spec;

		ether_addr_copy_swap(rfse.smac_h, eth_h->h_source);
		ether_addr_copy_swap(rfse.smac_m, eth_m->h_source);
		ether_addr_copy_swap(rfse.dmac_h, eth_h->h_dest);
		ether_addr_copy_swap(rfse.dmac_m, eth_m->h_dest);
		rfse.ethtype_h = ntohs(eth_h->h_proto);
		rfse.ethtype_m = ntohs(eth_m->h_proto);
		break;
	default:
		return -EOPNOTSUPP;
	}

	rfse.mode |= ENETC_RFSE_EN;
	if (fs->ring_cookie != RX_CLS_FLOW_DISC) {
		rfse.mode |= ENETC_RFSE_MODE_BD;
		rfse.result = fs->ring_cookie;
	}
done:
	return enetc_set_fs_entry(si, &rfse, fs->location);
}

static int enetc_get_rxnfc(struct net_device *ndev, struct ethtool_rxnfc *rxnfc,
			   u32 *rule_locs)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_si *si = priv->si;
	int i, j, max_entry_num;

	if (is_enetc_rev1(si))
		max_entry_num = si->num_fs_entries;
	else
		max_entry_num = priv->max_ipf_entries;

	switch (rxnfc->cmd) {
	case ETHTOOL_GRXRINGS:
		rxnfc->data = priv->num_rx_rings;
		break;
	case ETHTOOL_GRXFH:
		/* get RSS hash config */
		return enetc_get_rsshash(rxnfc);
	case ETHTOOL_GRXCLSRLCNT:
		/* total number of entries */
		rxnfc->data = max_entry_num;
		/* number of entries in use */
		rxnfc->rule_cnt = 0;
		for (i = 0; i < max_entry_num; i++)
			if (priv->cls_rules[i].used)
				rxnfc->rule_cnt++;
		break;
	case ETHTOOL_GRXCLSRULE:
		if (rxnfc->fs.location >= max_entry_num)
			return -EINVAL;

		/* get entry x */
		rxnfc->fs = priv->cls_rules[rxnfc->fs.location].fs;
		break;
	case ETHTOOL_GRXCLSRLALL:
		/* total number of entries */
		rxnfc->data = max_entry_num;
		/* array of indexes of used entries */
		j = 0;
		for (i = 0; i < max_entry_num; i++) {
			if (!priv->cls_rules[i].used)
				continue;
			if (j == rxnfc->rule_cnt)
				return -EMSGSIZE;
			rule_locs[j++] = i;
		}
		/* number of entries in use */
		rxnfc->rule_cnt = j;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int enetc4_set_wol_mg_ipft_entry(struct enetc_ndev_priv *priv)
{
	struct enetc_si *si = priv->si;
	struct ntmp_ipft_key *key __free(kfree);
	struct ntmp_ipft_cfg cfg;
	u32 val;
	int err;

	key = kzalloc(sizeof(*key), GFP_KERNEL);
	if (!key)
		return -ENOMEM;

	memset(&cfg, 0, sizeof(cfg));

	key->frm_attr_flags = NTMP_IPFT_FAF_WOL_MAGIC;
	key->frm_attr_flags_mask = key->frm_attr_flags;

	cfg.filter = BIT(0) | BIT(4) | (NTMP_IPFT_FLTA_SI_BITMAP << 5);
	cfg.flta_tgt = 1;

	err = ntmp_ipft_add_entry(&si->cbdr, key, &cfg, &priv->ipt_wol_eid);
	if (err)
		return err;

	val = enetc_port_rd(&si->hw, ENETC4_PIPFCR);
	if (!(val & PIPFCR_EN))
		/* Enable ingress port filter table lookup. */
		enetc_port_wr(&si->hw, ENETC4_PIPFCR, PIPFCR_EN);

	return 0;
}

static int enetc4_set_ipft_entry(struct enetc_si *si, struct ethtool_rx_flow_spec *fs,
				 u32 *entry_id)
{
	struct ethtool_tcpip4_spec *l4ip4_h, *l4ip4_m;
	struct ethtool_tcpip6_spec *l4ip6_h, *l4ip6_m;
	struct ethtool_usrip4_spec *l3ip4_h, *l3ip4_m;
	struct ethtool_usrip6_spec *l3ip6_h, *l3ip6_m;
	struct ethtool_flow_ext *h_ext, *m_ext;
	struct ethhdr *eth_h, *eth_m;
	struct ntmp_ipft_key *key;
	struct ntmp_ipft_cfg cfg;
	int err;

	key = kzalloc(sizeof(*key), GFP_KERNEL);
	if (!key)
		return -ENOMEM;

	memset(&cfg, 0, sizeof(cfg));

	if (fs->flow_type & FLOW_MAC_EXT) {
		ether_addr_copy(key->dmac, fs->h_ext.h_dest);
		ether_addr_copy(key->dmac_mask, fs->m_ext.h_dest);
	}

	if (fs->flow_type & FLOW_EXT) {
		int i;
		u8 *p;

		if (sizeof(h_ext->data) > NTMP_IPFT_MAX_PLD_LEN) {
			err = -EOPNOTSUPP;
			goto end;
		}

		h_ext = &fs->h_ext;
		m_ext = &fs->m_ext;
		for (i = 0, p = (u8 *)h_ext->data; i < sizeof(h_ext->data); i++, p++)
			key->byte[i].data = *p;

		for (i = 0, p = (u8 *)m_ext->data; i < sizeof(m_ext->data); i++, p++)
			key->byte[i].mask = *p;
	}

	switch (fs->flow_type & ~(FLOW_EXT | FLOW_MAC_EXT)) {
	case TCP_V4_FLOW:
		l4ip4_h = &fs->h_u.tcp_ip4_spec;
		l4ip4_m = &fs->m_u.tcp_ip4_spec;
		key->frm_attr_flags = NTMP_IPFT_FAF_IP_HDR | NTMP_IPFT_FAF_TCP_HDR;
		key->ip_protocol = IPPROTO_TCP;
		goto l4ip4;
	case UDP_V4_FLOW:
		l4ip4_h = &fs->h_u.udp_ip4_spec;
		l4ip4_m = &fs->m_u.udp_ip4_spec;
		key->frm_attr_flags = NTMP_IPFT_FAF_IP_HDR | NTMP_IPFT_FAF_UDP_HDR;
		key->ip_protocol = IPPROTO_UDP;
		goto l4ip4;
	case SCTP_V4_FLOW:
		l4ip4_h = &fs->h_u.sctp_ip4_spec;
		l4ip4_m = &fs->m_u.sctp_ip4_spec;
		key->frm_attr_flags = NTMP_IPFT_FAF_IP_HDR | NTMP_IPFT_FAF_SCTP_HDR;
		key->ip_protocol = IPPROTO_SCTP;
l4ip4:
		key->ip_protocol_mask = 0xff;
		key->ip_src[3] = l4ip4_h->ip4src;
		key->ip_src_mask[3] = l4ip4_m->ip4src;
		key->ip_dst[3] = l4ip4_h->ip4dst;
		key->ip_dst_mask[3] = l4ip4_m->ip4dst;
		key->l4_src_port = l4ip4_h->psrc;
		key->l4_src_port_mask = l4ip4_m->psrc;
		key->l4_dst_port = l4ip4_h->pdst;
		key->l4_dst_port_mask = l4ip4_m->pdst;
		key->ethertype = htons(ETH_P_IP);
		key->ethertype_mask = htons(0xffff);
		break;
	case TCP_V6_FLOW:
		l4ip6_h = &fs->h_u.tcp_ip6_spec;
		l4ip6_m = &fs->m_u.tcp_ip6_spec;
		key->frm_attr_flags = NTMP_IPFT_FAF_IP_HDR | NTMP_IPFT_FAF_IP_VER6 |
				      NTMP_IPFT_FAF_TCP_HDR;
		key->ip_protocol = IPPROTO_TCP;
		goto l4ip6;
	case UDP_V6_FLOW:
		l4ip6_h = &fs->h_u.udp_ip6_spec;
		l4ip6_m = &fs->m_u.udp_ip6_spec;
		key->frm_attr_flags = NTMP_IPFT_FAF_IP_HDR | NTMP_IPFT_FAF_IP_VER6 |
				      NTMP_IPFT_FAF_UDP_HDR;
		key->ip_protocol = IPPROTO_UDP;
		goto l4ip6;
	case SCTP_V6_FLOW:
		l4ip6_h = &fs->h_u.sctp_ip6_spec;
		l4ip6_m = &fs->m_u.sctp_ip6_spec;
		key->frm_attr_flags = NTMP_IPFT_FAF_IP_HDR | NTMP_IPFT_FAF_IP_VER6 |
				      NTMP_IPFT_FAF_SCTP_HDR;
		key->ip_protocol = IPPROTO_SCTP;

l4ip6:
		key->ip_protocol_mask = 0xff;
		memcpy(key->ip_src, l4ip6_h->ip6src, sizeof(key->ip_src));
		memcpy(key->ip_src_mask, l4ip6_m->ip6src, sizeof(key->ip_src_mask));
		memcpy(key->ip_dst, l4ip6_h->ip6dst, sizeof(key->ip_dst));
		memcpy(key->ip_dst_mask, l4ip6_m->ip6dst, sizeof(key->ip_dst_mask));
		key->l4_src_port = l4ip6_h->psrc;
		key->l4_src_port_mask = l4ip6_m->psrc;
		key->l4_dst_port = l4ip6_h->pdst;
		key->l4_dst_port_mask = l4ip6_m->pdst;
		key->ethertype = htons(ETH_P_IPV6);
		key->ethertype_mask = htons(0xffff);
		break;
	case IP_USER_FLOW:
		l3ip4_h = &fs->h_u.usr_ip4_spec;
		l3ip4_m = &fs->m_u.usr_ip4_spec;
		key->frm_attr_flags = NTMP_IPFT_FAF_IP_HDR;
		key->ip_src[3] = l3ip4_h->ip4src;
		key->ip_src_mask[3] = l3ip4_m->ip4src;
		key->ip_dst[3] = l3ip4_h->ip4dst;
		key->ip_dst_mask[3] = l3ip4_m->ip4dst;
		key->ip_protocol = l3ip4_h->proto;
		key->ip_protocol_mask = l3ip4_m->proto;
		key->ethertype = htons(ETH_P_IP);
		key->ethertype_mask = htons(0xffff);
		break;
	case IPV6_USER_FLOW:
		l3ip6_h = &fs->h_u.usr_ip6_spec;
		l3ip6_m = &fs->m_u.usr_ip6_spec;
		key->frm_attr_flags = NTMP_IPFT_FAF_IP_HDR | NTMP_IPFT_FAF_IP_VER6;
		memcpy(key->ip_src, l3ip6_h->ip6src, sizeof(key->ip_src));
		memcpy(key->ip_src_mask, l3ip6_m->ip6src, sizeof(key->ip_src_mask));
		memcpy(key->ip_dst, l3ip6_h->ip6dst, sizeof(key->ip_dst));
		memcpy(key->ip_dst_mask, l3ip6_m->ip6dst, sizeof(key->ip_dst_mask));
		key->ip_protocol = l3ip6_h->l4_proto;
		key->ip_protocol_mask = l3ip6_m->l4_proto;
		key->ethertype = htons(ETH_P_IPV6);
		key->ethertype_mask = htons(0xffff);
		break;
	case ETHER_FLOW:
		eth_h = &fs->h_u.ether_spec;
		eth_m = &fs->m_u.ether_spec;

		ether_addr_copy(key->smac, eth_h->h_source);
		ether_addr_copy(key->smac_mask, eth_m->h_source);
		ether_addr_copy(key->dmac, eth_h->h_dest);
		ether_addr_copy(key->dmac_mask, eth_m->h_dest);
		key->ethertype = eth_h->h_proto;
		key->ethertype_mask = eth_m->h_proto;
		break;
	}

	key->frm_attr_flags_mask = key->frm_attr_flags;

	if (fs->ring_cookie == RX_CLS_FLOW_WAKE) {
		cfg.filter = BIT(0) | BIT(4) | (NTMP_IPFT_FLTA_SI_BITMAP << 5);
		cfg.flta_tgt = 1;
	} else if (fs->ring_cookie == RX_CLS_FLOW_DISC) {
		cfg.filter = 0;
	}

	err = ntmp_ipft_add_entry(&si->cbdr, key, &cfg, entry_id);

end:
	kfree(key);

	return err;
}

static int enetc_validate_flow_rule(struct net_device *ndev, struct ethtool_rx_flow_spec *fs)
{
	struct ethtool_tcpip4_spec *l4ip4_m;
	struct ethtool_tcpip6_spec *l4ip6_m;
	struct ethtool_usrip4_spec *l3ip4_m;
	struct ethtool_usrip6_spec *l3ip6_m;

	switch (fs->flow_type & ~(FLOW_EXT | FLOW_MAC_EXT)) {
	case TCP_V4_FLOW:
		l4ip4_m = &fs->m_u.tcp_ip4_spec;
		goto l4ip4;
	case UDP_V4_FLOW:
		l4ip4_m = &fs->m_u.udp_ip4_spec;
		goto l4ip4;
	case SCTP_V4_FLOW:
		l4ip4_m = &fs->m_u.sctp_ip4_spec;

l4ip4:
		if (l4ip4_m->tos) {
			netdev_err(ndev, "Unsupport IPv4 tos filter\n");
			return -EINVAL;
		}
		break;
	case TCP_V6_FLOW:
		l4ip6_m = &fs->m_u.tcp_ip6_spec;
		goto l4ip6;
	case UDP_V6_FLOW:
		l4ip6_m = &fs->m_u.udp_ip6_spec;
		goto l4ip6;
	case SCTP_V6_FLOW:
		l4ip6_m = &fs->m_u.sctp_ip6_spec;

l4ip6:
		if (l4ip6_m->tclass) {
			netdev_err(ndev, "Unsupport IPv6 traffic class filter\n");
			return -EINVAL;
		}
		break;
	case IP_USER_FLOW:
		l3ip4_m = &fs->m_u.usr_ip4_spec;
		if (l3ip4_m->tos) {
			netdev_err(ndev, "Unsupport IPv4 tos filter\n");
			return -EINVAL;
		}
		break;
	case IPV6_USER_FLOW:
		l3ip6_m = &fs->m_u.usr_ip6_spec;
		if (l3ip6_m->tclass) {
			netdev_err(ndev, "Unsupport IPv6 traffic class filter\n");
			return -EINVAL;
		}
		break;
	case ETHER_FLOW:
		break;
	default:
		netdev_err(ndev, "Unsupported flow type (0x%x)\n", fs->flow_type);
		return -EINVAL;
	}

	if (fs->flow_type & FLOW_EXT) {
		if (fs->m_ext.vlan_etype || fs->m_ext.vlan_tci) {
			netdev_err(ndev, "Unsupport VLAN filter\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int enetc4_configure_rxnfc(struct net_device *ndev, struct ethtool_rxnfc *rxnfc)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_si *si = priv->si;
	u32 entry_id, val;
	int err;

	/* i.MX95 ENETC VF does not support Ingress Port Filter Table */
	if (!enetc_si_is_pf(si))
		return -EOPNOTSUPP;

	switch (rxnfc->cmd) {
	case ETHTOOL_SRXCLSRLINS:
		if (rxnfc->fs.location >= priv->max_ipf_entries)
			return -EINVAL;

		/* The hardware doesn't support specify a RX ring/queue
		 * index to deliver to.
		 */
		if (rxnfc->fs.ring_cookie != RX_CLS_FLOW_WAKE &&
		    rxnfc->fs.ring_cookie != RX_CLS_FLOW_DISC) {
			netdev_err(ndev, "Only support WOL and discard rules now\n");
			return -EINVAL;
		}

		err = enetc_validate_flow_rule(ndev, &rxnfc->fs);
		if (err)
			return err;

		/* If the rule index was used before, we need to delete the rule
		 * from the ingress port filter first, and then add the new rule
		 * entry into the ingress filter table.
		 */
		if (priv->cls_rules[rxnfc->fs.location].used) {
			struct enetc_cls_rule *cls_rule;

			cls_rule = &priv->cls_rules[rxnfc->fs.location];
			entry_id = cls_rule->entry_id;

			err = ntmp_ipft_delete_entry(&si->cbdr, entry_id);
			if (err)
				return err;

			memset(cls_rule, 0, sizeof(*cls_rule));
		}

		err = enetc4_set_ipft_entry(si, &rxnfc->fs, &entry_id);
		if (err)
			return err;

		val = enetc_port_rd(&si->hw, ENETC4_PIPFCR);
		if (!(val & PIPFCR_EN))
			/* Enable ingress port filter table lookup. */
			enetc_port_wr(&si->hw, ENETC4_PIPFCR, PIPFCR_EN);

		priv->cls_rules[rxnfc->fs.location].fs = rxnfc->fs;
		priv->cls_rules[rxnfc->fs.location].used = 1;
		priv->cls_rules[rxnfc->fs.location].entry_id = entry_id;
		break;
	case ETHTOOL_SRXCLSRLDEL:
		if (rxnfc->fs.location >= priv->max_ipf_entries)
			return -EINVAL;

		if (!priv->cls_rules[rxnfc->fs.location].used)
			return -EINVAL;

		entry_id = priv->cls_rules[rxnfc->fs.location].entry_id;
		err = ntmp_ipft_delete_entry(&si->cbdr, entry_id);
		if (err)
			return err;

		memset(&priv->cls_rules[rxnfc->fs.location], 0,
		       sizeof(priv->cls_rules[rxnfc->fs.location]));

		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int enetc_configure_rxnfc(struct net_device *ndev, struct ethtool_rxnfc *rxnfc)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_si *si = priv->si;
	int err;

	switch (rxnfc->cmd) {
	case ETHTOOL_SRXCLSRLINS:
		if (rxnfc->fs.location >= si->num_fs_entries)
			return -EINVAL;

		if (rxnfc->fs.ring_cookie >= priv->num_rx_rings &&
		    rxnfc->fs.ring_cookie != RX_CLS_FLOW_DISC)
			return -EINVAL;

		err = enetc_set_cls_entry(si, &rxnfc->fs, true);
		if (err)
			return err;
		priv->cls_rules[rxnfc->fs.location].fs = rxnfc->fs;
		priv->cls_rules[rxnfc->fs.location].used = 1;
		break;
	case ETHTOOL_SRXCLSRLDEL:
		if (rxnfc->fs.location >= si->num_fs_entries)
			return -EINVAL;

		err = enetc_set_cls_entry(si, &rxnfc->fs, false);
		if (err)
			return err;
		priv->cls_rules[rxnfc->fs.location].used = 0;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int enetc_set_rxnfc(struct net_device *ndev, struct ethtool_rxnfc *rxnfc)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);

	if (is_enetc_rev1(priv->si))
		return enetc_configure_rxnfc(ndev, rxnfc);
	else
		return enetc4_configure_rxnfc(ndev, rxnfc);
}

static u32 enetc_get_rxfh_key_size(struct net_device *ndev)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);

	/* return the size of the RX flow hash key.  PF only */
	return (priv->si->hw.port) ? ENETC_RSSHASH_KEY_SIZE : 0;
}

static u32 enetc_get_rxfh_indir_size(struct net_device *ndev)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);

	/* return the size of the RX flow hash indirection table */
	return priv->si->num_rss;
}

static int enetc_get_rxfh(struct net_device *ndev, u32 *indir, u8 *key,
			  u8 *hfunc)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_si *si = priv->si;
	struct enetc_hw *hw = &si->hw;
	int err = 0, i;

	/* return hash function */
	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP;

	/* return hash key */
	if (key && enetc_si_is_pf(si)) {
		u32 reg_off;

		for (i = 0; i < ENETC_RSSHASH_KEY_SIZE / 4; i++) {
			if (is_enetc_rev1(si))
				reg_off = ENETC_PRSSK(i);
			else
				reg_off = ENETC4_PRSSKR(i);

			((u32 *)key)[i] = enetc_port_rd(hw, reg_off);
		}
	}

	/* return RSS table */
	if (indir) {
		if (si->get_rss_table)
			err = si->get_rss_table(si, indir, si->num_rss);
		else
			err = -EOPNOTSUPP;
	}

	return err;
}

void enetc_set_rss_key(struct enetc_hw *hw, const u8 *bytes)
{
	struct enetc_si *si = container_of(hw, struct enetc_si, hw);
	int i;

	for (i = 0; i < ENETC_RSSHASH_KEY_SIZE / 4; i++)
		if (is_enetc_rev1(si))
			enetc_port_wr(hw, ENETC_PRSSK(i), ((u32 *)bytes)[i]);
		else
			enetc_port_wr(hw, ENETC4_PRSSKR(i), ((u32 *)bytes)[i]);
}
EXPORT_SYMBOL_GPL(enetc_set_rss_key);

static int enetc_set_rxfh(struct net_device *ndev, const u32 *indir,
			  const u8 *key, const u8 hfunc)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_si *si = priv->si;
	struct enetc_hw *hw = &si->hw;
	int err = 0;

	if (hfunc != ETH_RSS_HASH_NO_CHANGE && hfunc != ETH_RSS_HASH_TOP) {
		netdev_err(ndev, "unsupported hash function\n");
		return -EOPNOTSUPP;
	}

	/* set hash key, if PF */
	if (key && enetc_si_is_pf(si))
		enetc_set_rss_key(hw, key);

	/* set RSS table */
	if (indir) {
		if (si->set_rss_table)
			err = si->set_rss_table(si, indir, si->num_rss);
		else
			err = -EOPNOTSUPP;
	}

	return err;
}

static void enetc_get_channels(struct net_device *ndev,
			       struct ethtool_channels *ch)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);

	ch->max_rx = priv->num_rx_rings;
	ch->max_tx = priv->num_tx_rings;
	ch->rx_count = priv->num_rx_rings;
	ch->tx_count = priv->num_tx_rings;
}

static void enetc_get_ringparam(struct net_device *ndev,
				struct ethtool_ringparam *ring,
				struct kernel_ethtool_ringparam *kernel_ring,
				struct netlink_ext_ack *extack)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);

	ring->rx_pending = priv->rx_bd_count;
	ring->tx_pending = priv->tx_bd_count;

	/* do some h/w sanity checks for BDR length */
	if (netif_running(ndev)) {
		struct enetc_hw *hw = &priv->si->hw;
		u32 val = enetc_rxbdr_rd(hw, 0, ENETC_RBLENR);

		if (val != priv->rx_bd_count)
			netif_err(priv, hw, ndev, "RxBDR[RBLENR] = %d!\n", val);

		val = enetc_txbdr_rd(hw, 0, ENETC_TBLENR);

		if (val != priv->tx_bd_count)
			netif_err(priv, hw, ndev, "TxBDR[TBLENR] = %d!\n", val);
	}
}

static int enetc_get_coalesce(struct net_device *ndev,
			      struct ethtool_coalesce *ic,
			      struct kernel_ethtool_coalesce *kernel_coal,
			      struct netlink_ext_ack *extack)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_int_vector *v = priv->int_vector[0];
	u64 clk_freq = priv->si->clk_freq;

	ic->tx_coalesce_usecs = enetc_cycles_to_usecs(priv->tx_ictt, clk_freq);
	ic->rx_coalesce_usecs = enetc_cycles_to_usecs(v->rx_ictt, clk_freq);

	ic->tx_max_coalesced_frames = ENETC_TXIC_PKTTHR;
	ic->rx_max_coalesced_frames = ENETC_RXIC_PKTTHR;

	ic->use_adaptive_rx_coalesce = priv->ic_mode & ENETC_IC_RX_ADAPTIVE;

	return 0;
}

static int enetc_set_coalesce(struct net_device *ndev,
			      struct ethtool_coalesce *ic,
			      struct kernel_ethtool_coalesce *kernel_coal,
			      struct netlink_ext_ack *extack)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	u64 clk_freq = priv->si->clk_freq;
	u32 rx_ictt, tx_ictt;
	int i, ic_mode;
	bool changed;

	tx_ictt = enetc_usecs_to_cycles(ic->tx_coalesce_usecs, clk_freq);
	rx_ictt = enetc_usecs_to_cycles(ic->rx_coalesce_usecs, clk_freq);

	if (ic->rx_max_coalesced_frames != ENETC_RXIC_PKTTHR)
		return -EOPNOTSUPP;

	if (ic->tx_max_coalesced_frames != ENETC_TXIC_PKTTHR)
		return -EOPNOTSUPP;

	ic_mode = ENETC_IC_NONE;
	if (ic->use_adaptive_rx_coalesce) {
		ic_mode |= ENETC_IC_RX_ADAPTIVE;
		rx_ictt = 0x1;
	} else {
		ic_mode |= rx_ictt ? ENETC_IC_RX_MANUAL : 0;
	}

	ic_mode |= tx_ictt ? ENETC_IC_TX_MANUAL : 0;

	/* commit the settings */
	changed = (ic_mode != priv->ic_mode) || (priv->tx_ictt != tx_ictt);

	priv->ic_mode = ic_mode;
	priv->tx_ictt = tx_ictt;

	for (i = 0; i < priv->bdr_int_num; i++) {
		struct enetc_int_vector *v = priv->int_vector[i];

		v->rx_ictt = rx_ictt;
		v->rx_dim_en = !!(ic_mode & ENETC_IC_RX_ADAPTIVE);
	}

	if (netif_running(ndev) && changed) {
		/* reconfigure the operation mode of h/w interrupts,
		 * traffic needs to be paused in the process
		 */
		enetc_stop(ndev);
		enetc_start(ndev);
	}

	return 0;
}

static int enetc_get_ts_info(struct net_device *ndev,
			     struct ethtool_ts_info *info)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	int *phc_idx;

	if (is_enetc_rev1(priv->si)) {
		phc_idx = symbol_get(enetc_phc_index);
		if (phc_idx) {
			info->phc_index = *phc_idx;
			symbol_put(enetc_phc_index);
		} else {
			info->phc_index = -1;
		}
	} else {
		int domain;

		domain = pci_domain_nr(priv->si->pdev->bus);
		info->phc_index = netc_timer_get_phc_index(domain, 0,
							   PCI_DEVFN(24, 0));
	}

	if (enetc_ptp_clock_is_enabled(priv->si)) {
		info->so_timestamping = SOF_TIMESTAMPING_TX_HARDWARE |
					SOF_TIMESTAMPING_RX_HARDWARE |
					SOF_TIMESTAMPING_RAW_HARDWARE |
					SOF_TIMESTAMPING_TX_SOFTWARE |
					SOF_TIMESTAMPING_RX_SOFTWARE |
					SOF_TIMESTAMPING_SOFTWARE;

		info->tx_types = (1 << HWTSTAMP_TX_OFF) |
				 (1 << HWTSTAMP_TX_ON) |
				 (1 << HWTSTAMP_TX_ONESTEP_SYNC);
		info->rx_filters = (1 << HWTSTAMP_FILTER_NONE) |
				   (1 << HWTSTAMP_FILTER_ALL);
	} else {
		info->so_timestamping = SOF_TIMESTAMPING_RX_SOFTWARE |
					SOF_TIMESTAMPING_TX_SOFTWARE |
					SOF_TIMESTAMPING_SOFTWARE;
	}

	return 0;
}

static void enetc_get_wol(struct net_device *dev,
			  struct ethtool_wolinfo *wol)
{
	struct enetc_ndev_priv *priv = netdev_priv(dev);
	struct enetc_si *si = priv->si;
	struct enetc_pf *pf;

	wol->supported = 0;
	wol->wolopts = 0;
	pf = enetc_si_priv(si);

	if (pf->caps.wol) {
		if (device_can_wakeup(priv->dev)) {
			wol->supported = WAKE_MAGIC;
			wol->wolopts = priv->wolopts;
		}
	} else {
		if (dev->phydev)
			phy_ethtool_get_wol(dev->phydev, wol);
	}
}

static int enetc_set_wol(struct net_device *dev,
			 struct ethtool_wolinfo *wol)
{
	struct enetc_ndev_priv *priv = netdev_priv(dev);
	struct enetc_si *si = priv->si;
	u32 support = WAKE_MAGIC;
	struct enetc_pf *pf;
	int err;

	pf = enetc_si_priv(si);

	if (pf->caps.wol) {
		if (!device_can_wakeup(priv->dev) || wol->wolopts & ~support)
			return -EOPNOTSUPP;

		if (wol->wolopts == priv->wolopts)
			return 0;

		if (wol->wolopts) {
			err = enetc4_set_wol_mg_ipft_entry(priv);
			if (err)
				return err;
			if (priv->rcec && netc_ierb_may_wakeonlan() == 0) {
				priv->rcec->dev_flags |= PCI_DEV_FLAGS_NO_D3;
				device_set_wakeup_enable(&priv->rcec->dev, 1);
			}
			netc_ierb_enable_wakeonlan();
			netdev_info(dev, "enetc: wakeup enable\n");
		} else {
			netc_ierb_disable_wakeonlan();
			if (priv->rcec && netc_ierb_may_wakeonlan() == 0) {
				device_set_wakeup_enable(&priv->rcec->dev, 0);
				priv->rcec->dev_flags &= ~PCI_DEV_FLAGS_NO_D3;
			}
			err = ntmp_ipft_delete_entry(&priv->si->cbdr,
						     priv->ipt_wol_eid);
			if (err)
				return err;
			netdev_info(dev, "enetc: wakeup disable\n");
		}

		priv->wolopts = wol->wolopts;
	} else {
		if (!dev->phydev)
			return -EOPNOTSUPP;

		err = phy_ethtool_set_wol(dev->phydev, wol);
		if (!err) {
			device_set_wakeup_enable(&dev->dev, wol->wolopts);
			return err;
		}
	}

	return 0;
}

static int enetc_us_to_tx_cycle(struct net_device *dev, u32 *us)
{
	struct enetc_ndev_priv *priv = netdev_priv(dev);
	u32 cycle, max_us;

	max_us = enetc_cycles_to_usecs(PM_EEE_TIMER, priv->si->clk_freq);
	if (*us > max_us) {
		netdev_info(dev, "ENETC supports maximum tx_lpi_timer: %uus, using %uus instead.\n",
			    max_us, max_us);
		*us = max_us;
	}
	cycle = enetc_usecs_to_cycles(*us, priv->si->clk_freq);

	return cycle;
}

void enetc_eee_mode_set(struct net_device *dev, bool enable)
{
	struct enetc_ndev_priv *priv = netdev_priv(dev);
	unsigned int sleep_cycle = 0, wake_cycle = 0;
	struct ethtool_eee *eee = &priv->eee;
	struct enetc_si *si = priv->si;

	if (eee->eee_active) {
		if (enable) {
			sleep_cycle = enetc_us_to_tx_cycle(dev, &eee->tx_lpi_timer);
			wake_cycle = sleep_cycle;
		} else {
			eee->tx_lpi_timer = 0;
		}
		eee->eee_enabled = enable;
	}
	eee->tx_lpi_enabled = eee->eee_active;

	enetc_port_mac_wr(si, ENETC4_PM_SLEEP_TIMER(0), sleep_cycle);
	enetc_port_mac_wr(si, ENETC4_PM_LPWAKE_TIMER(0), wake_cycle);
}
EXPORT_SYMBOL_GPL(enetc_eee_mode_set);

static int enetc_ethtool_op_get_eee(struct net_device *dev,
				    struct ethtool_eee *edata)
{
	struct enetc_ndev_priv *priv = netdev_priv(dev);
	struct enetc_si *si = priv->si;

	if (is_enetc_rev1(si))
		return -EOPNOTSUPP;

	edata->eee_enabled = priv->eee.eee_enabled;
	edata->tx_lpi_timer = priv->eee.tx_lpi_timer;
	edata->tx_lpi_enabled = priv->eee.tx_lpi_enabled;

	return phylink_ethtool_get_eee(priv->phylink, edata);
}

static int enetc_ethtool_op_set_eee(struct net_device *dev,
				    struct ethtool_eee *edata)
{
	struct enetc_ndev_priv *priv = netdev_priv(dev);
	struct ethtool_eee *eee = &priv->eee;

	if (is_enetc_rev1(priv->si))
		return -EOPNOTSUPP;

	if (!netif_running(dev))
		return -ENETDOWN;

	eee->tx_lpi_timer = edata->tx_lpi_timer;

	if (!edata->eee_enabled || !edata->tx_lpi_enabled ||
	    !edata->tx_lpi_timer) {
		enetc_eee_mode_set(dev, false);
		if (edata->eee_enabled) {
			netdev_info(dev, "Please set tx_lpi_timer at same time. EEE not enabled.\n");
			edata->eee_enabled = false;
		}
	} else {
		enetc_eee_mode_set(dev, true);
	}

	return phylink_ethtool_set_eee(priv->phylink, edata);
}

static void enetc_get_pauseparam(struct net_device *dev,
				 struct ethtool_pauseparam *pause)
{
	struct enetc_ndev_priv *priv = netdev_priv(dev);

	phylink_ethtool_get_pauseparam(priv->phylink, pause);
}

static int enetc_set_pauseparam(struct net_device *dev,
				struct ethtool_pauseparam *pause)
{
	struct enetc_ndev_priv *priv = netdev_priv(dev);

	return phylink_ethtool_set_pauseparam(priv->phylink, pause);
}

static int enetc_get_link_ksettings(struct net_device *dev,
				    struct ethtool_link_ksettings *cmd)
{
	struct enetc_ndev_priv *priv = netdev_priv(dev);

	if (!priv->phylink)
		return -EOPNOTSUPP;

	return phylink_ethtool_ksettings_get(priv->phylink, cmd);
}

static int enetc_set_link_ksettings(struct net_device *dev,
				    const struct ethtool_link_ksettings *cmd)
{
	struct enetc_ndev_priv *priv = netdev_priv(dev);

	if (!priv->phylink)
		return -EOPNOTSUPP;

	return phylink_ethtool_ksettings_set(priv->phylink, cmd);
}

static void enetc_get_mm_stats(struct net_device *ndev,
			       struct ethtool_mm_stats *s)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_hw *hw = &priv->si->hw;
	struct enetc_si *si = priv->si;

	if (!(si->hw_features & ENETC_SI_F_QBU))
		return;

	if (is_enetc_rev1(si)) {
		s->MACMergeFrameAssErrorCount = enetc_port_rd(hw, ENETC_MMFAECR);
		s->MACMergeFrameSmdErrorCount = enetc_port_rd(hw, ENETC_MMFSECR);
		s->MACMergeFrameAssOkCount = enetc_port_rd(hw, ENETC_MMFAOCR);
		s->MACMergeFragCountRx = enetc_port_rd(hw, ENETC_MMFCRXR);
		s->MACMergeFragCountTx = enetc_port_rd(hw, ENETC_MMFCTXR);
		s->MACMergeHoldCount = enetc_port_rd(hw, ENETC_MMHCR);
	} else {
		s->MACMergeFrameAssErrorCount = enetc_port_rd(hw, ENETC4_MMFAECR);
		s->MACMergeFrameSmdErrorCount = enetc_port_rd(hw, ENETC4_MMFSECR);
		s->MACMergeFrameAssOkCount = enetc_port_rd(hw, ENETC4_MMFAOCR);
		s->MACMergeFragCountRx = enetc_port_rd(hw, ENETC4_MMFCRXR);
		s->MACMergeFragCountTx = enetc_port_rd(hw, ENETC4_MMFCTXR);
		s->MACMergeHoldCount = enetc_port_rd(hw, ENETC4_MMHCR);
	}
}

static int enetc_get_mm(struct net_device *ndev, struct ethtool_mm_state *state)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_si *si = priv->si;
	struct enetc_hw *hw = &si->hw;
	u32 lafs, rafs, val;

	if (!(si->hw_features & ENETC_SI_F_QBU))
		return -EOPNOTSUPP;

	mutex_lock(&priv->mm_lock);

	if (is_enetc_rev1(si)) {
		val = enetc_port_rd(hw, ENETC_PFPMR);
		state->pmac_enabled = !!(val & ENETC_PFPMR_PMACE);

		val = enetc_port_rd(hw, ENETC_MMCSR);
	} else {
		val = enetc_port_rd(hw, ENETC4_MMCSR);
		state->pmac_enabled = !!(val & MMCSR_ME);
	}

	switch (ENETC_MMCSR_GET_VSTS(val)) {
	case 0:
		state->verify_status = ETHTOOL_MM_VERIFY_STATUS_DISABLED;
		break;
	case 2:
		state->verify_status = ETHTOOL_MM_VERIFY_STATUS_VERIFYING;
		break;
	case 3:
		state->verify_status = ETHTOOL_MM_VERIFY_STATUS_SUCCEEDED;
		break;
	case 4:
		state->verify_status = ETHTOOL_MM_VERIFY_STATUS_FAILED;
		break;
	case 5:
	default:
		state->verify_status = ETHTOOL_MM_VERIFY_STATUS_UNKNOWN;
		break;
	}

	rafs = ENETC_MMCSR_GET_RAFS(val);
	state->tx_min_frag_size = ethtool_mm_frag_size_add_to_min(rafs);
	lafs = ENETC_MMCSR_GET_LAFS(val);
	state->rx_min_frag_size = ethtool_mm_frag_size_add_to_min(lafs);
	state->tx_enabled = !!(val & ENETC_MMCSR_LPE); /* mirror of MMCSR_ME */
	state->tx_active = state->tx_enabled &&
			   (state->verify_status == ETHTOOL_MM_VERIFY_STATUS_SUCCEEDED ||
			    state->verify_status == ETHTOOL_MM_VERIFY_STATUS_DISABLED);
	state->verify_enabled = !(val & ENETC_MMCSR_VDIS);
	state->verify_time = ENETC_MMCSR_GET_VT(val);
	/* A verifyTime of 128 ms would exceed the 7 bit width
	 * of the ENETC_MMCSR_VT field
	 */
	state->max_verify_time = 127;

	mutex_unlock(&priv->mm_lock);

	return 0;
}

static int enetc_mm_wait_tx_active(struct enetc_hw *hw, int verify_time)
{
	int timeout = verify_time * USEC_PER_MSEC * ENETC_MM_VERIFY_RETRIES;
	u32 val;

	/* This will time out after the standard value of 3 verification
	 * attempts. To not sleep forever, it relies on a non-zero verify_time,
	 * guarantee which is provided by the ethtool nlattr policy.
	 */
	return read_poll_timeout(enetc_port_rd, val,
				 ENETC_MMCSR_GET_VSTS(val) == 3,
				 ENETC_MM_VERIFY_SLEEP_US, timeout,
				 true, hw, ENETC_MMCSR);
}

static int enetc4_mm_wait_tx_active(struct enetc_hw *hw, int verify_time)
{
	int timeout = verify_time * USEC_PER_MSEC * ENETC_MM_VERIFY_RETRIES;
	u32 val;

	return read_poll_timeout(enetc_port_rd, val,
				 MMCSR_GET_VSTS(val) == MMCSR_VSTS_SUCCESSFUL,
				 ENETC_MM_VERIFY_SLEEP_US, timeout,
				 true, hw, ENETC4_MMCSR);
}

static void enetc_set_ptcfpr(struct enetc_hw *hw, u8 preemptible_tcs)
{
	u32 val;
	int tc;

	for (tc = 0; tc < 8; tc++) {
		val = enetc_port_rd(hw, ENETC_PTCFPR(tc));

		if (preemptible_tcs & BIT(tc))
			val |= ENETC_PTCFPR_FPE;
		else
			val &= ~ENETC_PTCFPR_FPE;

		enetc_port_wr(hw, ENETC_PTCFPR(tc), val);
	}
}

static void enetc4_set_pfpcr(struct enetc_hw *hw, u8 preemptible_tcs)
{
	enetc_port_wr(hw, ENETC4_PFPCR, preemptible_tcs);
}

/* ENETC does not have an IRQ to notify changes to the MAC Merge TX status
 * (active/inactive), but the preemptible traffic classes should only be
 * committed to hardware once TX is active. Resort to polling.
 */
void enetc_mm_commit_preemptible_tcs(struct enetc_ndev_priv *priv)
{
	struct enetc_si *si = priv->si;
	struct enetc_hw *hw = &si->hw;
	u8 preemptible_tcs = 0;
	u32 val;
	int err;

	if (is_enetc_rev1(si)) {
		val = enetc_port_rd(hw, ENETC_MMCSR);
		if (!(val & ENETC_MMCSR_ME))
			goto out;

		if (!(val & ENETC_MMCSR_VDIS)) {
			err = enetc_mm_wait_tx_active(hw, ENETC_MMCSR_GET_VT(val));
			if (err)
				goto out;
		}
	} else {
		val = enetc_port_rd(hw, ENETC4_MMCSR);
		if (!(val & MMCSR_ME))
			goto out;

		if (!(val & MMCSR_VDIS)) {
			err = enetc4_mm_wait_tx_active(hw, MMCSR_GET_VT(val));
			if (err)
				goto out;
		}
	}

	preemptible_tcs = priv->preemptible_tcs;
out:
	if (is_enetc_rev1(si))
		enetc_set_ptcfpr(hw, preemptible_tcs);
	else
		enetc4_set_pfpcr(hw, preemptible_tcs);
}

/* FIXME: Workaround for the link partner's verification failing if ENETC
 * priorly received too much express traffic. The documentation doesn't
 * suggest this is needed.
 */
static void enetc_restart_emac_rx(struct enetc_si *si)
{
	struct enetc_hw *hw = &si->hw;
	u32 val;

	if (is_enetc_rev1(si)) {
		val = enetc_port_rd(hw, ENETC_PM0_CMD_CFG);

		enetc_port_wr(hw, ENETC_PM0_CMD_CFG, val & ~ENETC_PM0_RX_EN);

		if (val & ENETC_PM0_RX_EN)
			enetc_port_wr(hw, ENETC_PM0_CMD_CFG, val);
	} else {
		val = enetc_port_rd(hw, ENETC4_PM_CMD_CFG(0));

		enetc_port_wr(hw, ENETC4_PM_CMD_CFG(0), val & ~PM_CMD_CFG_RX_EN);

		if (val & PM_CMD_CFG_RX_EN)
			enetc_port_wr(hw, ENETC4_PM_CMD_CFG(0), val);
	}
}

static void enetc_mm_config(struct enetc_ndev_priv *priv, struct ethtool_mm_cfg *cfg,
			    u32 add_frag_size)
{
	struct enetc_si *si = priv->si;
	struct enetc_hw *hw = &si->hw;
	u32 val;

	val = enetc_port_rd(hw, ENETC_PFPMR);
	if (cfg->pmac_enabled)
		val |= ENETC_PFPMR_PMACE;
	else
		val &= ~ENETC_PFPMR_PMACE;
	enetc_port_wr(hw, ENETC_PFPMR, val);

	val = enetc_port_rd(hw, ENETC_MMCSR);

	if (cfg->verify_enabled)
		val &= ~ENETC_MMCSR_VDIS;
	else
		val |= ENETC_MMCSR_VDIS;

	if (cfg->tx_enabled)
		priv->active_offloads |= ENETC_F_QBU;
	else
		priv->active_offloads &= ~ENETC_F_QBU;

	/* If link is up, enable/disable MAC Merge right away */
	if (!(val & ENETC_MMCSR_LINK_FAIL)) {
		if (!!(priv->active_offloads & ENETC_F_QBU))
			val |= ENETC_MMCSR_ME;
		else
			val &= ~ENETC_MMCSR_ME;
	}

	val &= ~ENETC_MMCSR_VT_MASK;
	val |= ENETC_MMCSR_VT(cfg->verify_time);

	val &= ~ENETC_MMCSR_RAFS_MASK;
	val |= ENETC_MMCSR_RAFS(add_frag_size);

	enetc_port_wr(hw, ENETC_MMCSR, val);

	enetc_restart_emac_rx(si);

	enetc_mm_commit_preemptible_tcs(priv);
}

static void enetc4_mm_config(struct enetc_ndev_priv *priv, struct ethtool_mm_cfg *cfg,
			     u32 add_frag_size)
{
	struct enetc_si *si = priv->si;
	struct enetc_hw *hw = &si->hw;
	u32 val;

	val = enetc_port_rd(hw, ENETC4_MMCSR);
	if (cfg->pmac_enabled)
		val = u32_replace_bits(val, MMCSR_ME_4B_BOUNDARY, MMCSR_ME);
	else
		val = u32_replace_bits(val, 0, MMCSR_ME);

	if (cfg->verify_enabled)
		val &= ~MMCSR_VDIS;
	else
		val |= MMCSR_VDIS;

	if (cfg->tx_enabled) {
		priv->active_offloads |= ENETC_F_QBU;

		/* When preemption is enabled on a port, IEEE 1588 PTP
		 * one-step timestamping is not supported.
		 */
		priv->active_offloads &= ~ENETC_F_TX_ONESTEP_SYNC_TSTAMP;
	} else {
		priv->active_offloads &= ~ENETC_F_QBU;
	}

	/* If link is up, enable/disable MAC Merge right away */
	if (!(val & MMCSR_LINK_FAIL)) {
		if (!!(priv->active_offloads & ENETC_F_QBU)) {
			val = u32_replace_bits(val, MMCSR_ME_4B_BOUNDARY, MMCSR_ME);

			/* When preemption is enabled, generation of PAUSE must be
			 * disabled.
			 */
			enetc_port_wr(hw, ENETC4_PPAUONTR, 0);
			enetc_port_wr(hw, ENETC4_PPAUOFFTR, 0);
		} else {
			val = u32_replace_bits(val, 0, MMCSR_ME);
		}
	}

	val = u32_replace_bits(val, cfg->verify_time, MMCSR_VT);
	val = u32_replace_bits(val, add_frag_size, MMCSR_RAFS);

	enetc_port_wr(hw, ENETC4_MMCSR, val);

	enetc_restart_emac_rx(si);

	enetc_mm_commit_preemptible_tcs(priv);
}

static int enetc_set_mm(struct net_device *ndev, struct ethtool_mm_cfg *cfg,
			struct netlink_ext_ack *extack)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_si *si = priv->si;
	u32 add_frag_size;
	int err;

	if (!(si->hw_features & ENETC_SI_F_QBU))
		return -EOPNOTSUPP;

	err = ethtool_mm_frag_size_min_to_add(cfg->tx_min_frag_size,
					      &add_frag_size, extack);
	if (err)
		return err;

	mutex_lock(&priv->mm_lock);

	if (is_enetc_rev1(si))
		enetc_mm_config(priv, cfg, add_frag_size);
	else
		enetc4_mm_config(priv, cfg, add_frag_size);

	mutex_unlock(&priv->mm_lock);

	return 0;
}

/* When the link is lost, the verification state machine goes to the FAILED
 * state and doesn't restart on its own after a new link up event.
 * According to 802.3 Figure 99-8 - Verify state diagram, the LINK_FAIL bit
 * should have been sufficient to re-trigger verification, but for ENETC it
 * doesn't. As a workaround, we need to toggle the Merge Enable bit to
 * re-trigger verification when link comes up.
 */
void enetc_mm_link_state_update(struct enetc_ndev_priv *priv, bool link)
{
	struct enetc_si *si = priv->si;
	struct enetc_hw *hw = &si->hw;
	u32 val;

	mutex_lock(&priv->mm_lock);

	if (is_enetc_rev1(si)) {
		val = enetc_port_rd(hw, ENETC_MMCSR);

		if (link) {
			val &= ~ENETC_MMCSR_LINK_FAIL;
			if (priv->active_offloads & ENETC_F_QBU)
				val |= ENETC_MMCSR_ME;
		} else {
			val |= ENETC_MMCSR_LINK_FAIL;
			if (priv->active_offloads & ENETC_F_QBU)
				val &= ~ENETC_MMCSR_ME;
		}

		enetc_port_wr(hw, ENETC_MMCSR, val);
	} else {
		val = enetc_port_rd(hw, ENETC4_MMCSR);

		if (link) {
			val &= ~MMCSR_LINK_FAIL;
			if (priv->active_offloads & ENETC_F_QBU)
				val = u32_replace_bits(val, MMCSR_ME_4B_BOUNDARY,
						       MMCSR_ME);
		} else {
			val |= MMCSR_LINK_FAIL;
			if (priv->active_offloads & ENETC_F_QBU)
				val = u32_replace_bits(val, 0, MMCSR_ME);
		}

		enetc_port_wr(hw, ENETC4_MMCSR, val);
	}

	enetc_mm_commit_preemptible_tcs(priv);

	mutex_unlock(&priv->mm_lock);
}
EXPORT_SYMBOL_GPL(enetc_mm_link_state_update);

static const struct ethtool_ops enetc_pf_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_USECS |
				     ETHTOOL_COALESCE_MAX_FRAMES |
				     ETHTOOL_COALESCE_USE_ADAPTIVE_RX,
	.get_regs_len = enetc_get_reglen,
	.get_regs = enetc_get_regs,
	.get_sset_count = enetc_get_sset_count,
	.get_strings = enetc_get_strings,
	.get_ethtool_stats = enetc_get_ethtool_stats,
	.get_pause_stats = enetc_get_pause_stats,
	.get_rmon_stats = enetc_get_rmon_stats,
	.get_eth_ctrl_stats = enetc_get_eth_ctrl_stats,
	.get_eth_mac_stats = enetc_get_eth_mac_stats,
	.get_rxnfc = enetc_get_rxnfc,
	.set_rxnfc = enetc_set_rxnfc,
	.get_rxfh_key_size = enetc_get_rxfh_key_size,
	.get_rxfh_indir_size = enetc_get_rxfh_indir_size,
	.get_rxfh = enetc_get_rxfh,
	.set_rxfh = enetc_set_rxfh,
	.get_channels = enetc_get_channels,
	.get_ringparam = enetc_get_ringparam,
	.get_coalesce = enetc_get_coalesce,
	.set_coalesce = enetc_set_coalesce,
	.get_link_ksettings = enetc_get_link_ksettings,
	.set_link_ksettings = enetc_set_link_ksettings,
	.get_link = ethtool_op_get_link,
	.get_ts_info = enetc_get_ts_info,
	.get_wol = enetc_get_wol,
	.set_wol = enetc_set_wol,
	.get_eee = enetc_ethtool_op_get_eee,
	.set_eee = enetc_ethtool_op_set_eee,
	.get_pauseparam = enetc_get_pauseparam,
	.set_pauseparam = enetc_set_pauseparam,
	.get_mm = enetc_get_mm,
	.set_mm = enetc_set_mm,
	.get_mm_stats = enetc_get_mm_stats,
};

static const struct ethtool_ops enetc_vf_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_USECS |
				     ETHTOOL_COALESCE_MAX_FRAMES |
				     ETHTOOL_COALESCE_USE_ADAPTIVE_RX,
	.get_regs_len = enetc_get_reglen,
	.get_regs = enetc_get_regs,
	.get_sset_count = enetc_get_sset_count,
	.get_strings = enetc_get_strings,
	.get_ethtool_stats = enetc_get_ethtool_stats,
	.get_rxnfc = enetc_get_rxnfc,
	.set_rxnfc = enetc_set_rxnfc,
	.get_rxfh_indir_size = enetc_get_rxfh_indir_size,
	.get_rxfh = enetc_get_rxfh,
	.set_rxfh = enetc_set_rxfh,
	.get_channels = enetc_get_channels,
	.get_ringparam = enetc_get_ringparam,
	.get_coalesce = enetc_get_coalesce,
	.set_coalesce = enetc_set_coalesce,
	.get_link = ethtool_op_get_link,
	.get_ts_info = enetc_get_ts_info,
};

void enetc_set_ethtool_ops(struct net_device *ndev)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);

	if (enetc_si_is_pf(priv->si))
		ndev->ethtool_ops = &enetc_pf_ethtool_ops;
	else
		ndev->ethtool_ops = &enetc_vf_ethtool_ops;
}
EXPORT_SYMBOL_GPL(enetc_set_ethtool_ops);