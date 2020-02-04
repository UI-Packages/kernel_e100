/**********************************************************************
 * Author: Cavium, Inc.
 *
 * Contact: support@cavium.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2012 Cavium, Inc.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this file; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 * or visit http://www.gnu.org/licenses/.
 *
 * This file may also be available under a different license from Cavium.
 * Contact Cavium, Inc. for more information
 **********************************************************************/
#include <linux/kernel.h>
#include <linux/ethtool.h>
#include <linux/phy.h>
#include <linux/ratelimit.h>
#include <linux/of_mdio.h>
#include <linux/net_tstamp.h>

#include <net/dst.h>

#include <asm/octeon/octeon.h>
#include <asm/octeon/cvmx-srio.h>
#include <asm/octeon/octeon-ethernet-user.h>

#include "ethernet-defines.h"
#include "octeon-ethernet.h"

#include <asm/octeon/cvmx-helper-board.h>

#include <asm/octeon/cvmx-smix-defs.h>
#include <asm/octeon/cvmx-gmxx-defs.h>
#include <asm/octeon/cvmx-pip-defs.h>
#include <asm/octeon/cvmx-pko-defs.h>

static void cvm_oct_get_drvinfo(struct net_device *dev,
				struct ethtool_drvinfo *info)
{
	strcpy(info->driver, "octeon-ethernet");
	strcpy(info->version, OCTEON_ETHERNET_VERSION);
	strcpy(info->bus_info, "Builtin");
}

static int cvm_oct_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	cvmx_helper_link_info_t link_info;
	struct octeon_ethernet *priv = netdev_priv(dev);

	if (priv->phydev)
		return phy_ethtool_gset(priv->phydev, cmd);

	link_info = cvmx_helper_link_get(priv->ipd_port);
	cmd->speed = link_info.s.link_up ? link_info.s.speed : 0;
	cmd->duplex = link_info.s.full_duplex ? DUPLEX_FULL : DUPLEX_HALF;

	return 0;
}

static int cvm_oct_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct octeon_ethernet *priv = netdev_priv(dev);

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (priv->phydev)
		return phy_ethtool_sset(priv->phydev, cmd);

	return -EOPNOTSUPP;
}

static int cvm_oct_nway_reset(struct net_device *dev)
{
	struct octeon_ethernet *priv = netdev_priv(dev);

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (priv->phydev)
		return phy_start_aneg(priv->phydev);

	return -EOPNOTSUPP;
}

const struct ethtool_ops cvm_oct_ethtool_ops = {
	.get_drvinfo = cvm_oct_get_drvinfo,
	.get_settings = cvm_oct_get_settings,
	.set_settings = cvm_oct_set_settings,
	.nway_reset = cvm_oct_nway_reset,
	.get_link = ethtool_op_get_link,
};

/**
 * cvm_oct_ioctl_hwtstamp - IOCTL support for timestamping
 * @dev:    Device to change
 * @rq:     the request
 * @cmd:    the command
 *
 * Returns Zero on success
 */
static int cvm_oct_ioctl_hwtstamp(struct net_device *dev,
				  struct ifreq *rq, int cmd)
{
	struct octeon_ethernet *priv = netdev_priv(dev);
	struct hwtstamp_config config;
	union cvmx_mio_ptp_clock_cfg ptp;
	union cvmx_gmxx_rxx_frm_ctl frm_ctl;
	union cvmx_pip_prt_cfgx prt_cfg;

	if (copy_from_user(&config, rq->ifr_data, sizeof(config)))
		return -EFAULT;

	if (config.flags) /* reserved for future extensions */
		return -EINVAL;

	/* Check the status of hardware for tiemstamps */
	if (OCTEON_IS_MODEL(OCTEON_CN6XXX) || OCTEON_IS_MODEL(OCTEON_CNF7XXX)) {
		/* Write TX timestamp into word 4 */
		cvmx_write_csr(CVMX_PKO_REG_TIMESTAMP, 4);

		switch (priv->imode) {
		case CVMX_HELPER_INTERFACE_MODE_XAUI:
		case CVMX_HELPER_INTERFACE_MODE_RXAUI:
		case CVMX_HELPER_INTERFACE_MODE_SGMII:
			break;
		default:
			/* No timestamp support*/
			return -EOPNOTSUPP;
		}

		ptp.u64 = octeon_read_ptp_csr(CVMX_MIO_PTP_CLOCK_CFG);
		if (!ptp.s.ptp_en) {
			/* It should have been enabled by csrc-octeon-ptp */
			netdev_err(dev, "Error: PTP clock not enabled\n");
			/* No timestamp support*/
			return -EOPNOTSUPP;
		}
	} else {
			/* No timestamp support*/
			return -EOPNOTSUPP;
	}

	switch (config.tx_type) {
	case HWTSTAMP_TX_OFF:
		priv->tx_timestamp_hw = 0;
		break;
	case HWTSTAMP_TX_ON:
		priv->tx_timestamp_hw = 1;
		break;
	default:
		return -ERANGE;
	}

	switch (config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		priv->rx_timestamp_hw = 0;

		frm_ctl.u64 = cvmx_read_csr(CVMX_GMXX_RXX_FRM_CTL(priv->interface_port, priv->interface));
		frm_ctl.s.ptp_mode = 0;
		cvmx_write_csr(CVMX_GMXX_RXX_FRM_CTL(priv->interface_port, priv->interface), frm_ctl.u64);

		prt_cfg.u64 = cvmx_read_csr(CVMX_PIP_PRT_CFGX(priv->ipd_pkind));
		prt_cfg.s.skip = 0;
		cvmx_write_csr(CVMX_PIP_PRT_CFGX(priv->ipd_pkind), prt_cfg.u64);
		break;
	case HWTSTAMP_FILTER_ALL:
	case HWTSTAMP_FILTER_SOME:
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		priv->rx_timestamp_hw = 1;
		config.rx_filter = HWTSTAMP_FILTER_ALL;
		frm_ctl.u64 = cvmx_read_csr(CVMX_GMXX_RXX_FRM_CTL(priv->interface_port, priv->interface));
		frm_ctl.s.ptp_mode = 1;
		cvmx_write_csr(CVMX_GMXX_RXX_FRM_CTL(priv->interface_port, priv->interface), frm_ctl.u64);

		prt_cfg.u64 = cvmx_read_csr(CVMX_PIP_PRT_CFGX(priv->ipd_pkind));
		prt_cfg.s.skip = 8;
		cvmx_write_csr(CVMX_PIP_PRT_CFGX(priv->ipd_pkind), prt_cfg.u64);
		break;
	default:
		return -ERANGE;
	}

	if (copy_to_user(rq->ifr_data, &config, sizeof(config)))
		return -EFAULT;

	return 0;
}

/**
 * cvm_oct_ioctl - IOCTL support for PHY control
 * @dev:    Device to change
 * @rq:     the request
 * @cmd:    the command
 *
 * Returns Zero on success
 */
int cvm_oct_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct octeon_ethernet *priv = netdev_priv(dev);
	union cvmx_srio_tx_message_header tx_header;
	int ivalue;

	switch (cmd) {
	case CAVIUM_NET_IOCTL_SETPRIO:
		ivalue = rq->ifr_ifru.ifru_ivalue;
		if ((ivalue >= 0) && (ivalue < 4)) {
			tx_header.u64 = priv->srio_tx_header;
			tx_header.s.prio = ivalue;
			priv->srio_tx_header = tx_header.u64;
			return 0;
		}
		return -EINVAL;

	case CAVIUM_NET_IOCTL_GETPRIO:
		tx_header.u64 = priv->srio_tx_header;
		rq->ifr_ifru.ifru_ivalue = tx_header.s.prio;
		return 0;

	case CAVIUM_NET_IOCTL_SETIDSIZE:
		ivalue = rq->ifr_ifru.ifru_ivalue;
		if ((ivalue >= 0) && (ivalue < 2)) {
			tx_header.u64 = priv->srio_tx_header;
			tx_header.s.tt = ivalue;
			priv->srio_tx_header = tx_header.u64;
			return 0;
		}
		return -EINVAL;

	case CAVIUM_NET_IOCTL_GETIDSIZE:
		tx_header.u64 = priv->srio_tx_header;
		rq->ifr_ifru.ifru_ivalue = tx_header.s.tt;
		return 0;

	case CAVIUM_NET_IOCTL_SETSRCID:
		ivalue = rq->ifr_ifru.ifru_ivalue;
		if ((ivalue >= 0) && (ivalue < 2)) {
			tx_header.u64 = priv->srio_tx_header;
			tx_header.s.sis = ivalue;
			priv->srio_tx_header = tx_header.u64;
			return 0;
		}
		return -EINVAL;

	case CAVIUM_NET_IOCTL_GETSRCID:
		tx_header.u64 = priv->srio_tx_header;
		rq->ifr_ifru.ifru_ivalue = tx_header.s.sis;
		return 0;

	case CAVIUM_NET_IOCTL_SETLETTER:
		ivalue = rq->ifr_ifru.ifru_ivalue;
		if ((ivalue >= -1) && (ivalue < 4)) {
			tx_header.u64 = priv->srio_tx_header;
			tx_header.s.lns = (ivalue == -1);
			if (tx_header.s.lns)
				tx_header.s.letter = 0;
			else
				tx_header.s.letter = ivalue;
			priv->srio_tx_header = tx_header.u64;
			return 0;
		}
		return -EINVAL;

	case CAVIUM_NET_IOCTL_GETLETTER:
		tx_header.u64 = priv->srio_tx_header;
		if (tx_header.s.lns)
			rq->ifr_ifru.ifru_ivalue = -1;
		else
			rq->ifr_ifru.ifru_ivalue = tx_header.s.letter;
		return 0;

	case SIOCSHWTSTAMP:
		return cvm_oct_ioctl_hwtstamp(dev, rq, cmd);

	default:
		if (priv->phydev)
			return phy_mii_ioctl(priv->phydev, rq, cmd);
	}
	return -EOPNOTSUPP;
}

static void cvm_oct_note_carrier(struct octeon_ethernet *priv,
				 cvmx_helper_link_info_t li)
{
	if (li.s.link_up) {
		pr_notice_ratelimited("%s: %u Mbps %s duplex, port %d\n",
				      netdev_name(priv->netdev), li.s.speed,
				      (li.s.full_duplex) ? "Full" : "Half",
				      priv->ipd_port);
	} else {
		pr_notice_ratelimited("%s: Link down\n",
				      netdev_name(priv->netdev));
	}
}

/**
 * cvm_oct_set_carrier - common wrapper of netif_carrier_{on,off}
 *
 * @priv: Device struct.
 * @link_info: Current state.
 */
void cvm_oct_set_carrier(struct octeon_ethernet *priv,
			 cvmx_helper_link_info_t link_info)
{
	cvm_oct_note_carrier(priv, link_info);
	if (link_info.s.link_up) {
		if (!netif_carrier_ok(priv->netdev))
			netif_carrier_on(priv->netdev);
	} else {
		if (netif_carrier_ok(priv->netdev))
			netif_carrier_off(priv->netdev);
	}
}

void cvm_oct_adjust_link(struct net_device *dev)
{
	struct octeon_ethernet *priv = netdev_priv(dev);
	cvmx_helper_link_info_t link_info;

	if (priv->last_link != priv->phydev->link) {
		priv->last_link = priv->phydev->link;
		link_info.u64 = 0;
		link_info.s.link_up = priv->last_link ? 1 : 0;
		link_info.s.full_duplex = priv->phydev->duplex ? 1 : 0;
		link_info.s.speed = priv->phydev->speed;

		cvmx_helper_link_set(priv->ipd_port, link_info);

		if (priv->link_change)
			priv->link_change(priv, link_info);

		cvm_oct_note_carrier(priv, link_info);
	}
}

int cvm_oct_common_stop(struct net_device *dev)
{
	struct octeon_ethernet *priv = netdev_priv(dev);
	cvmx_helper_link_info_t link_info;

	if (priv->last_link) {
		link_info.u64 = 0;
		priv->last_link = 0;

		cvmx_helper_link_set(priv->ipd_port, link_info);

		if (priv->link_change)
			priv->link_change(priv, link_info);

		cvm_oct_note_carrier(priv, link_info);
	}
	return 0;
}

/**
 * cvm_oct_phy_setup_device - setup the PHY
 *
 * @dev:    Device to setup
 *
 * Returns Zero on success, negative on failure
 */
int cvm_oct_phy_setup_device(struct net_device *dev)
{
	struct octeon_ethernet *priv = netdev_priv(dev);
	struct device_node *phy_node;

	if (!priv->of_node)
		goto no_phy;

	phy_node = of_parse_phandle(priv->of_node, "phy-handle", 0);
	if (!phy_node)
		goto no_phy;

	priv->phydev = of_phy_connect(dev, phy_node, cvm_oct_adjust_link, 0,
				      PHY_INTERFACE_MODE_GMII);

	if (priv->phydev == NULL)
		return -ENODEV;

	priv->last_link = 0;
	phy_start_aneg(priv->phydev);

	return 0;
no_phy:
	/* If there is no phy, assume a direct MAC connection and that
	 * the link is up.
	 */
	netif_carrier_on(dev);
	return 0;
}
