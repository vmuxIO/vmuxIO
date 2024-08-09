/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2015 Intel Corporation
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <format>
#include <fcntl.h>
#include <rte_mbuf_ptype.h>
#include <rte_memcpy.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include "sims/nic/e810_bm/e810_ptp.h"
#include "src/util.hpp"
#include "src/drivers/driver.hpp"
#include "src/drivers/flow_blocks.hpp"
#include <unistd.h>

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024

#define NUM_MBUFS 256 // queue size
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32

// from dpdk/app/test/packet_burst_generator.c
static void
copy_buf_to_pkt_segs(void *buf, unsigned len, struct rte_mbuf *pkt,
		unsigned offset)
{
	struct rte_mbuf *seg;
	void *seg_buf;
	unsigned copy_len;

	seg = pkt;
	while (offset >= seg->data_len) {
		offset -= seg->data_len;
		seg = seg->next;
	}
	copy_len = seg->data_len - offset;
	seg_buf = rte_pktmbuf_mtod_offset(seg, char *, offset);
	while (len > copy_len) {
		rte_memcpy(seg_buf, buf, (size_t) copy_len);
		len -= copy_len;
		buf = ((char *) buf + copy_len);
		seg = seg->next;
		seg_buf = rte_pktmbuf_mtod(seg, void *);
	}
	rte_memcpy(seg_buf, buf, (size_t) len);
}

// from dpdk/app/test/packet_burst_generator.c
static inline void
copy_buf_to_pkt(void *buf, unsigned len, struct rte_mbuf *pkt, unsigned offset)
{
	if (offset + len <= pkt->data_len) {
		rte_memcpy(rte_pktmbuf_mtod_offset(pkt, char *, offset), buf,
			   (size_t) len);
		return;
	}
	copy_buf_to_pkt_segs(buf, len, pkt, offset);
}

/* Port initialization used in flow filtering. 8< */
static void
filtering_init_port(uint16_t port_id, uint16_t nr_queues, std::vector<struct rte_mempool*> &rx_mbuf_pools, std::vector<struct rte_mempool*> &tx_mbuf_pools)
{
	int ret;
	uint16_t i;
	/* Ethernet port configured with default settings. 8< */
	struct rte_eth_conf port_conf = {
		.rxmode = {
			.offloads = 
				RTE_ETH_RX_OFFLOAD_TIMESTAMP
		},
		.txmode = {
			.offloads =
				RTE_ETH_TX_OFFLOAD_VLAN_INSERT |
				RTE_ETH_TX_OFFLOAD_IPV4_CKSUM  |
				RTE_ETH_TX_OFFLOAD_UDP_CKSUM   |
				RTE_ETH_TX_OFFLOAD_TCP_CKSUM   |
				RTE_ETH_TX_OFFLOAD_SCTP_CKSUM  |
				RTE_ETH_TX_OFFLOAD_TCP_TSO	   |
				RTE_ETH_TX_OFFLOAD_MULTI_SEGS, 
		},


	};
	struct rte_eth_txconf txq_conf;
	struct rte_eth_rxconf rxq_conf;
	struct rte_eth_dev_info dev_info;

	ret = rte_eth_dev_info_get(port_id, &dev_info);
	if (ret != 0)
		rte_exit(EXIT_FAILURE,
			"Error during getting device (port %u) info: %s\n",
			port_id, strerror(-ret));

	port_conf.txmode.offloads &= dev_info.tx_offload_capa;
	printf(":: initializing port: %d\n", port_id);
	ret = rte_eth_dev_configure(port_id,
				nr_queues, nr_queues, &port_conf);
	if (ret < 0) {
		rte_exit(EXIT_FAILURE,
			":: cannot configure device: err=%d, port=%u\n",
			ret, port_id);
	}

	rxq_conf = dev_info.default_rxconf;
	rxq_conf.offloads = port_conf.rxmode.offloads;
	/* >8 End of ethernet port configured with default settings. */

	/* Configuring number of RX and TX queues connected to single port. 8< */
	struct rte_mempool *rx_pool;
	size_t magic = 36; // when we set the PTP capability on the pNIC, bigger rx bursts can cause problems that look like as if the pool was exhausted (leaked mbufs). This magic threashold fixes it. Decrement by one to get the error again.
	for (i = 0; i < nr_queues; i++) {
		size_t rx_buffers = NUM_MBUFS + magic;
		// TODO allocate these elsewhere
		rx_pool = rte_pktmbuf_pool_create(std::format("RX_MBUF_POOL_{}", i).c_str(), rx_buffers ,
			64, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id()); // TODO constant for cache
		if (rx_pool == NULL)
			rte_exit(EXIT_FAILURE, "Cannot create rx mbuf pool %d\n", i);
		rx_mbuf_pools.push_back(rx_pool);

		ret = rte_eth_rx_queue_setup(port_id, i, NUM_MBUFS,
				     rte_eth_dev_socket_id(port_id),
				     &rxq_conf,
				     rx_pool);
		if (ret < 0) {
			rte_exit(EXIT_FAILURE,
				":: Rx queue setup failed: err=%d, port=%u\n",
				ret, port_id);
		}
	}

	txq_conf = dev_info.default_txconf;
	txq_conf.offloads = port_conf.txmode.offloads;

	struct rte_mempool *tx_pool;
	for (i = 0; i < nr_queues; i++) {
		size_t tx_buffers = NUM_MBUFS; // TODO
		// TODO allocate these elsewhere
		tx_pool = rte_pktmbuf_pool_create(std::format("TX_MBUF_POOL_{}", i).c_str(), tx_buffers ,
			64, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id()); // TODO constant for cache
		if (tx_pool == NULL)
			rte_exit(EXIT_FAILURE, "Cannot create tx mbuf pool %d\n", i);
		tx_mbuf_pools.push_back(tx_pool);

		ret = rte_eth_tx_queue_setup(port_id, i, NUM_MBUFS,
				rte_eth_dev_socket_id(port_id),
				&txq_conf);
		if (ret < 0) {
			rte_exit(EXIT_FAILURE,
				":: Tx queue setup failed: err=%d, port=%u\n",
				ret, port_id);
		}
	}
	/* >8 End of Configuring RX and TX queues connected to single port. */

	/* Setting the RX port to promiscuous mode. 8< */
	ret = rte_eth_promiscuous_enable(port_id);
	if (ret != 0)
		rte_exit(EXIT_FAILURE,
			":: promiscuous mode enable failed: err=%s, port=%u\n",
			rte_strerror(-ret), port_id);
	/* >8 End of setting the RX port to promiscuous mode. */

	/* Starting the port. 8< */
	ret = rte_eth_dev_start(port_id);
	if (ret < 0) {
		rte_exit(EXIT_FAILURE,
			"rte_eth_dev_start:err=%d, port=%u\n",
			ret, port_id);
	}
	/* >8 End of starting the port. */
  	
	printf(":: initializing port: %d done\n", port_id);
}
/* >8 End of Port initialization used in flow filtering. */


/* basicfwd.c: Basic DPDK skeleton forwarding example. */

/*
 * Initializes a given port using global settings and with the RX buffers
 * coming from the mbuf_pool passed as a parameter.
 */

/* Main functional part of port initialization. 8< */
static inline int
port_init(uint16_t port, struct rte_mempool *mbuf_pool)
{
	struct rte_eth_conf port_conf;
	const uint16_t rx_rings = 1, tx_rings = 1;
	uint16_t nb_rxd = RX_RING_SIZE;
	uint16_t nb_txd = TX_RING_SIZE;
	int retval;
	uint16_t q;
	struct rte_eth_dev_info dev_info;
	struct rte_eth_txconf txconf;
	
	if (!rte_eth_dev_is_valid_port(port))
		return -1;

	memset(&port_conf, 0, sizeof(struct rte_eth_conf));

	retval = rte_eth_dev_info_get(port, &dev_info);
	if (retval != 0) {
		printf("Error during getting device (port %u) info: %s\n",
				port, strerror(-retval));
		return retval;
	}

	if (dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE)
		port_conf.txmode.offloads |=
			RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;

	/* Configure the Ethernet device. */
	retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
	if (retval != 0)
		return retval;

	retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
	if (retval != 0)
		return retval;

	/* Allocate and set up 1 RX queue per Ethernet port. */
	for (q = 0; q < rx_rings; q++) {
		retval = rte_eth_rx_queue_setup(port, q, nb_rxd,
				rte_eth_dev_socket_id(port), NULL, mbuf_pool);
		if (retval < 0)
			return retval;
	}

	txconf = dev_info.default_txconf;
	txconf.offloads = port_conf.txmode.offloads;
	/* Allocate and set up 1 TX queue per Ethernet port. */
	for (q = 0; q < tx_rings; q++) {
		retval = rte_eth_tx_queue_setup(port, q, nb_txd,
				rte_eth_dev_socket_id(port), &txconf);
		if (retval < 0)
			return retval;
	}

	/* Starting Ethernet port. 8< */
	retval = rte_eth_dev_start(port);
	/* >8 End of starting of ethernet port. */
	if (retval < 0)
		return retval;

	/* Display the port MAC address. */
	struct rte_ether_addr addr;
	retval = rte_eth_macaddr_get(port, &addr);
	if (retval != 0)
		return retval;

	printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
			   " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
			port, RTE_ETHER_ADDR_BYTES(&addr));

 
  	/* Enable RX in promiscuous mode for the Ethernet device. */
	retval = rte_eth_promiscuous_enable(port);
	
	/* End of setting RX port in promiscuous mode. */
	if (retval != 0)
		return retval;
	
	return 0;
}
/* >8 End of main functional part of port initialization. */


static void
lcore_poll_once(void) {
	uint16_t port;

	/*
	 * Receive packets on a port and forward them on the same
	 * port.
	 */
	RTE_ETH_FOREACH_DEV(port) {

		/* Get burst of RX packets, from first port of pair. */
		struct rte_mbuf *bufs[BURST_SIZE];
		const uint16_t nb_rx = rte_eth_rx_burst(port, 0,
				bufs, BURST_SIZE);

		if (unlikely(nb_rx == 0))
			continue;

		if_log_level(LOG_DEBUG,
			for (int i = 0; i < nb_rx; i++) {
				struct rte_mbuf* buf = bufs[i];
				if (buf->l2_type == RTE_PTYPE_L2_ETHER) {
					// struct rte_ether_hdr* header = (struct rte_ether_hdr*) buf.buf_addr;
					struct rte_ether_hdr* header = rte_pktmbuf_mtod(bufs[i], struct rte_ether_hdr *);
					char src_addr[RTE_ETHER_ADDR_FMT_SIZE];
					char dst_addr[RTE_ETHER_ADDR_FMT_SIZE];
					rte_ether_format_addr(src_addr, RTE_ETHER_ADDR_FMT_SIZE, &header->src_addr);
					rte_ether_format_addr(dst_addr, RTE_ETHER_ADDR_FMT_SIZE, &header->dst_addr);
					printf("ethernet (%d bytes) %s -> %s\n", buf->buf_len, src_addr, dst_addr);
				} else {
					printf("non ethernet packet: l2 type: %d\n", buf->l2_type);
				}
			}
		);

		/* Send burst of TX packets, back to same port. */
		const uint16_t nb_tx = rte_eth_tx_burst(port, 0,
				bufs, nb_rx);

		/* Free any unsent packets. */
		if (unlikely(nb_tx < nb_rx)) {
			uint16_t buf;
			for (buf = nb_tx; buf < nb_rx; buf++)
				rte_pktmbuf_free(bufs[buf]);
		}
	}
}

static void lcore_init_checks() {
	uint16_t port;

	/*
	 * Check that the port is on the same NUMA node as the polling thread
	 * for best performance.
	 */
	RTE_ETH_FOREACH_DEV(port)
		if (rte_eth_dev_socket_id(port) >= 0 &&
				rte_eth_dev_socket_id(port) !=
						(int)rte_socket_id())
			printf("WARNING, port %u is on remote NUMA node to "
					"polling thread.\n\tPerformance will "
					"not be optimal.\n", port);
}


class Dpdk : public Driver {
private:
	const static uint16_t max_queues_per_vm = 4;

	// struct rte_mempool *mbuf_pool;
	std::vector<struct rte_mempool*> tx_mbuf_pools;
	std::vector<struct rte_mempool*> rx_mbuf_pools;
	struct rte_mbuf **bufs; // list of rte_mbuf pointers
	uint16_t port_id;
	std::vector<bool> mediate; // per VM


	// get queue id of native queue
	uint16_t get_rx_queue_id(int vm, int queue) {
		return vm * this->max_queues_per_vm + queue;
	}

	uint16_t get_tx_queue_id(int vm, int queue) {
		return vm * this->max_queues_per_vm + queue;
	}

public:
	Dpdk(int num_vms, const uint8_t (*mac_addr)[6], int argc, char *argv[]) {
		this->alloc_rx_lists(this->max_queues_per_vm * num_vms, BURST_SIZE);
    this->bufs = (struct rte_mbuf **) malloc(this->max_queues_per_vm * BURST_SIZE * num_vms * sizeof(struct rte_mbuf*));
		this->mediate = std::vector<bool>(num_vms, false);

		/*
 	 	 * The main function, which does initialization and calls the per-lcore
 	 	 * functions.
 	 	 */
		struct rte_mempool *mbuf_pool;
		unsigned nb_ports;
		uint16_t portid;

		/* Initializion the Environment Abstraction Layer (EAL). 8< */
		int ret = rte_eal_init(argc, argv);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
		/* >8 End of initialization the Environment Abstraction Layer (EAL). */

		argc -= ret;
		argv += ret;

		/* Check that there is an even number of ports to send/receive on. */
		nb_ports = rte_eth_dev_count_avail();
		if (nb_ports != 1)
			rte_exit(EXIT_FAILURE, "Error: number of ports must be 1. Is %d.\n", nb_ports);

		/* Creates a new mempool in memory to hold the mbufs. */

		// /* Allocates mempool to hold the mbufs. 8< */
		// size_t rx_buffers = NUM_MBUFS * nb_ports * num_vms * this->max_queues_per_vm;
		// size_t tx_buffers = rx_buffers; // TODO remove
		// mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", rx_buffers + tx_buffers ,
		// 	MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
		// this->mbuf_pool = mbuf_pool;
		// /* >8 End of allocating mempool to hold mbuf. */
		//
		// if (mbuf_pool == NULL)
		// 	rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

		static uint16_t port_id = 0;
		static uint16_t nr_queues = num_vms * this->max_queues_per_vm;
		struct rte_flow *flow;
		struct rte_flow_error error;
		this->port_id = port_id;

		/* Initializing all ports. 8< */
		filtering_init_port(port_id, nr_queues, this->rx_mbuf_pools, this->tx_mbuf_pools);
		// RTE_ETH_FOREACH_DEV(portid)
		// 	if (port_init(portid, mbuf_pool) != 0)
		// 		rte_exit(EXIT_FAILURE, "Cannot init port %" PRIu16 "\n",
		// 				portid);
		/* >8 End of initializing all ports. */

		/* Create flow for send packet with. 8< */
		/* closing and releasing resources */
		ret = rte_flow_flush(this->port_id, &error);
		if (ret != 0) {
			printf("Flow can't be flushed %d message: %s\n",
				error.type,
				error.message ? error.message : "(no stated reason)");
			rte_exit(EXIT_FAILURE, "error in flushing flows");
		}

#define SRC_IP ((0<<24) + (0<<16) + (0<<8) + 0) /* src ip = 0.0.0.0 */
#define DEST_IP ((192<<24) + (168<<16) + (1<<8) + 1) /* dest ip = 192.168.1.1 */
#define FULL_MASK 0xffffffff /* full mask */
#define EMPTY_MASK 0x0 /* empty mask */

		struct rte_ether_addr src_mac;
		struct rte_ether_addr src_mask;
		struct rte_ether_addr dest_mac;
		struct rte_ether_addr dest_mask;
		rte_ether_unformat_addr("00:00:00:00:00:00", &src_mac);
		rte_ether_unformat_addr("00:00:00:00:00:00", &src_mask);
		// rte_ether_unformat_addr("52:54:00:fa:00:60", &dest_mac);
		rte_ether_unformat_addr("FF:FF:FF:FF:FF:FF", &dest_mask);

		for (int queue_nb = 0; queue_nb < num_vms; queue_nb++) {
			memcpy(&dest_mac, mac_addr, 6);
			Util::intcrement_mac((uint8_t*)&dest_mac, queue_nb);
			// send all VM traffic to the first queue of each VM by default
			flow = generate_eth_flow(port_id, this->get_rx_queue_id(queue_nb, 0),
						&src_mac, &src_mask,
						&dest_mac, &dest_mask, &error);
			/* >8 End of create flow and the flow rule. */
			if (!flow) {
			printf("Flow can't be created %d message: %s\n",
				error.type,
				error.message ? error.message : "(no stated reason)");
			rte_exit(EXIT_FAILURE, "error in creating flow");
			}
			/* >8 End of creating flow for send packet with. */
		}


		if (rte_lcore_count() > 1)
			printf("\nWARNING: Too many lcores enabled. Only 1 used.\n");
	}

	virtual ~Dpdk() {
		// TODO (peter) dealloc rxBufs etc...
		struct rte_flow_error error;
		int ret;

		/* closing and releasing resources */
		rte_flow_flush(this->port_id, &error);
    	rte_eth_timesync_disable(this->port_id);   
		
    	ret = rte_eth_dev_stop(this->port_id);
	
  		if (ret < 0) {
			printf("Failed to stop port %u: %s",
		       	 this->port_id, rte_strerror(-ret));
    	}

		rte_eth_dev_close(this->port_id);
		/* clean up the EAL */
		rte_eal_cleanup();
	}

	// blocks, busy waiting!
	void poll_once() {
		lcore_init_checks();
		lcore_poll_once();

		/* Call lcore_main on the main core only. Called on single lcore. 8< */
		// lcore_main();
		/* >8 End of called on single lcore. */
	}

	virtual void send(int vm_id, const char *buf, const size_t len) {
		// lcore_init_checks(); ignore cpu locality for now
		uint16_t port;
		RTE_ETH_FOREACH_DEV(port) {
			// prepare packet buffer
			uint16_t queue = this->get_tx_queue_id(vm_id, 0);
			struct rte_mbuf *pkt;
			pkt = rte_pktmbuf_alloc(this->tx_mbuf_pools[queue]);
			if (pkt == NULL) {
				printf("WARN: Dpdk::send: alloc failed\n");
				return; // drop packet
			}
			pkt->data_len = len;
			pkt->pkt_len = len;
			pkt->nb_segs = 1;

			// TODO	
			pkt->ol_flags |= RTE_MBUF_F_TX_IEEE1588_TMST; 
		
			copy_buf_to_pkt((void*)buf, len, pkt, 0);
			
			/* Send burst of TX packets. */
			const uint16_t nb_tx = rte_eth_tx_burst(port, queue,
					&pkt, 1);
			if (nb_tx != 1) {
				printf("\nWARNING: Sending packet failed. \n");
			}
			if_log_level(LOG_DEBUG, printf("send: "));
			if_log_level(LOG_DEBUG, Util::dump_pkt((void*)buf, len));

			/* Free packets. */
			rte_pktmbuf_free(pkt);
		}
	}

	// each recv(vm) call must be followed up with a recv_consumed(vm) call. No other VMs may receive in between. Otherwise it is unclear which VM owns which buffers
  virtual void recv(int vm_id) {
		// lcore_init_checks(); ignore cpu locality for now
		uint16_t port = 0;

		/*
	 	 * Receive packets on a port and forward them on the same
	 	 * port.
	 	 */
		for (int q_idx = 0; q_idx < this->max_queues_per_vm; q_idx++) {
			int queue_id = this->get_rx_queue_id(vm_id, q_idx);

			/* Get burst of RX packets, from first port of pair. */
			const uint16_t nb_rx = rte_eth_rx_burst(port, queue_id,
					&(this->bufs[queue_id * BURST_SIZE]), BURST_SIZE);

			if (unlikely(nb_rx == 0))
				continue;
				// continue;

			// pass pointers to packet buffers via rxBufs to behavioral model
			for (uint16_t i = queue_id * BURST_SIZE; i < (queue_id * BURST_SIZE + nb_rx); i++) {
				struct rte_mbuf* buf = this->bufs[i]; // we checked before that there is at least one packet
				char* pkt = rte_pktmbuf_mtod(buf, char*);
				if (buf->nb_segs != 1)
					die("This rx buffer has multiple segments. Unimplemented.");
				if (buf->pkt_len >= this->MAX_BUF)
					die("Cant handle packets of size %d", buf->pkt_len);
				// rte_memcpy(this->rxBufs[i], pkt, buf->pkt_len);
				this->rxBufs[i] = pkt;
				this->rxBuf_used[i] = buf->pkt_len;
				if (this->mediate[vm_id]) {
					this->rxBuf_queue[i] = q_idx;
				} else {
					// make the behavioral model emulate the switching
					this->rxBuf_queue[i] = {};
				}
				if_log_level(LOG_DEBUG, printf("recv queue %d: ", queue_id));
				if_log_level(LOG_DEBUG, Util::dump_pkt(this->rxBufs[i], this->rxBuf_used[i]));
			}
			this->nb_bufs_used[queue_id] = nb_rx;
		}
  }

  virtual void recv_consumed(int vm_id) {
    // free pkt
		for (int q_idx = 0; q_idx < this->max_queues_per_vm; q_idx++) {
			int queue_id = this->get_rx_queue_id(vm_id, q_idx);
			for (uint16_t i = queue_id * BURST_SIZE; i < (queue_id * BURST_SIZE + this->nb_bufs_used[queue_id]); i++) {
				rte_pktmbuf_free(this->bufs[i]);
			}
			this->nb_bufs_used[queue_id] = 0;
		}
  }
 
  /* Enables Timesync / PTP */
  virtual void enableTimesync(uint16_t port) {
	uint32_t retval = rte_eth_timesync_enable(port);
	if (retval < 0) {
		perror("Could not enable Timesync");
	}
  } 
  
  /* Get timestamp from NIC (global clock 0) */
  struct timespec readCurrentTimestamp() {

	struct timespec ts = { .tv_sec=0, .tv_nsec=0 };
    if(rte_eth_timesync_read_time(0, &ts)) {
		perror("Dpdk current time failed!");
	}
    
	return ts;
  }

  uint64_t readTxTimestamp(uint16_t portid) {
	struct timespec ts = { .tv_sec=0, .tv_nsec=0 };

	if(rte_eth_timesync_read_tx_timestamp(portid, &ts)) {
		perror("Dpdk TX timestamp failed!");
	}

	uint64_t tstamp = (TIMESPEC_TO_NANOS(ts) & 0xFFFFFFFFFF);
	
	return tstamp;
  };
  
  uint64_t readRxTimestamp(uint16_t portid) {
	struct timespec ts = { .tv_sec=0, .tv_nsec=0 };

	if(rte_eth_timesync_read_rx_timestamp(portid, &ts, 0)) {
		perror("Dpdk RX timestamp failed!");
	}

	// dpdk adds the timestamp and the global time together, so we split up the timestamp
	uint64_t tstamp = (TIMESPEC_TO_NANOS(ts) & 0xFFFFFFFFFF);

	return tstamp;
  };

  virtual bool add_switch_rule(int vm_id, uint8_t dst_addr[6], uint16_t dst_queue) {
  	if (!this->mediate[vm_id]) {
  		// for emulation we ignore switch rules.
  		// Because we don't send queue hints to the behavioral model, it emulates the switch then.
  		return true;
  	}

  	printf("dpdk add switch rule\n");

		struct rte_flow_error error;
  	struct rte_flow* flow;
		struct rte_ether_addr src_mac;
		struct rte_ether_addr src_mask;
		struct rte_ether_addr dest_mac;
		struct rte_ether_addr dest_mask;
		char fmt[20];
		uint16_t queue_id = this->get_rx_queue_id(vm_id, dst_queue);

		rte_ether_unformat_addr("00:00:00:00:00:00", &src_mac);
		rte_ether_unformat_addr("00:00:00:00:00:00", &src_mask);
		// rte_ether_unformat_addr("52:54:00:fa:00:60", &dest_mac);
		rte_ether_unformat_addr("FF:FF:FF:FF:FF:FF", &dest_mask);

		memcpy(dest_mac.addr_bytes, dst_addr, 6);
		flow = generate_eth_flow(port_id, queue_id,
					&src_mac, &src_mask,
					&dest_mac, &dest_mask, &error);
		if (!flow) {
			printf("Flow can't be created %d message: %s\n",
				error.type,
				error.message ? error.message : "(no stated reason)");
			return false;
		}
		rte_ether_format_addr(fmt, sizeof(fmt), &dest_mac);
  	printf("added rule dst_mac %s -> queue %d\n", fmt, queue_id);

  	return true;
  }

  virtual bool mediation_enable(int vm_id) {
		this->mediate[vm_id] = true;
		return true;
  }

  virtual bool mediation_disable(int vm_id) {
		// die("unimplemented: you need to flush switch rules from the emulator to the device");
		return false;
  }

  virtual bool is_mediating(int vm_id) {
	return this->mediate[vm_id];
  }
};
