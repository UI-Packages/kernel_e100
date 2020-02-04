/*********************************************************************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2007 Cavium Networks
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
 * Contact Cavium Networks for more information
*********************************************************************/

int cvm_oct_xmit(struct sk_buff *skb, struct net_device *dev);
int cvm_oct_xmit_lockless(struct sk_buff *skb, struct net_device *dev);
int cvm_oct_xmit_pow(struct sk_buff *skb, struct net_device *dev);

/* OUTPUT QOS support. Prioritizing TX traffic by selecting an output 
 * queue base on the packet's DSCP/TOS value.
 */
#ifndef CVM_QOS_OUTPUT_QOS
#define CVM_QOS_OUTPUT_QOS
#endif

/* As the ethernet interfaces come under INTERFACE1, 
 * CVMX_PKO_QUEUES_PER_PORT_INTERFACE1 is referred. 
 * TBD: The same queue selection logic exists in cavium-ipfwd-offload 
 * module as well. Need to do code cleanup.
 */
#ifdef CVM_QOS_OUTPUT_QOS
#define QOS_OUTPUT_MAP ((CVMX_PKO_QUEUES_PER_PORT_INTERFACE1 >= 8) ? 0x01234567 : \
                        ((CVMX_PKO_QUEUES_PER_PORT_INTERFACE1 == 7) ? 0x01234556 : \
                        ((CVMX_PKO_QUEUES_PER_PORT_INTERFACE1 == 6) ? 0x01233445 : \
                        ((CVMX_PKO_QUEUES_PER_PORT_INTERFACE1 == 5) ? 0x01122334 : \
                        ((CVMX_PKO_QUEUES_PER_PORT_INTERFACE1 == 4) ? 0x00112233 : \
                        ((CVMX_PKO_QUEUES_PER_PORT_INTERFACE1 == 3) ? 0x00011122 : \
                        ((CVMX_PKO_QUEUES_PER_PORT_INTERFACE1 == 2) ? 0x00001111 : \
                        0x00000000)))))))

#define GET_SKBUFF_OUTPUT_QOS(skb) ((QOS_OUTPUT_MAP >> \
                        ((((ip_hdr(skb)->tos) >> 5) & 0x07) * 4)) & 0x07)
#endif
