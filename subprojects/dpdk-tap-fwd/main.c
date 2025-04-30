#include <stdlib.h>
#include <stdio.h>

#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_arp.h>
#include <linux/if_tun.h>
#include <linux/virtio_net.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <rte_common.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_launch.h>
#include <rte_lcore.h>

#define HDR_SIZE 12

bool cksum_supported;
bool tso_supported;
bool uso_supported;

struct rte_mempool *tx_pool;
struct rte_mempool *rx_pool;
struct rte_ether_addr mac_addr;
int tap_fd = -1;

void init_memory(void);
void init_device(void);
void init_tap(const char *);

void poll_tx(void);
int poll_rx(void *);
void poll_txrx(void);

void do_tx(void);
uint16_t do_rx(void);

int main(int argc, char *argv[]) {
    int ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        rte_exit(EXIT_FAILURE, "Invalid EAL arguments");
    }

    argc -= ret;
    argv += ret;

    if (argc > 2 || (argc == 2 && argv[1][0] == '-')) {
        rte_exit(EXIT_FAILURE, "Unknown parameters in command line.");
    }

    const char *tapname = NULL;
    if (argc >= 2) {
        tapname = argv[1];
    }

    if (rte_eth_dev_count_avail() != 1) {
       rte_exit(EXIT_FAILURE, "Number of ports must be 1.");
    }

    init_memory();
    init_device();
    init_tap(tapname);

    printf("CKSUM: %d, TSO: %d, USO: %d\n", cksum_supported, tso_supported, uso_supported);

    unsigned lcore_id = rte_lcore_id();
    unsigned rx_lcore_id = rte_get_next_lcore(lcore_id, 1, 0);

    if (rx_lcore_id == RTE_MAX_LCORE || rx_lcore_id == lcore_id) {
        // We only have one lcore
        puts("Single core polling.");
        poll_txrx();
    } else {
        // Do RX on second lcore
        puts("Two core polling.");
        ret = rte_eal_remote_launch(poll_rx, NULL, rx_lcore_id);
        if (ret != 0) {
            rte_exit(EXIT_FAILURE, "Failed to launch RX function.");
        }

        poll_tx();
    }
}

void init_memory() {
    tx_pool = rte_pktmbuf_pool_create("TX_POOL", 512 - 1, 32, 0, UINT16_MAX, SOCKET_ID_ANY);

    static_assert(RTE_PKTMBUF_HEADROOM >= HDR_SIZE, "Headroom needs to be large enough for tap vnethdr");
    rx_pool = rte_pktmbuf_pool_create("RX_POOL", 512 - 1, 32, 0, RTE_MBUF_DEFAULT_BUF_SIZE, SOCKET_ID_ANY);
}

void init_device() {
    struct rte_eth_dev_info dev_info;
    int ret = rte_eth_dev_info_get(0, &dev_info);
    if (ret != 0) {
        rte_exit(EXIT_FAILURE, "Failed to get device info.");
    }
    uint64_t cksum_offload_flags = RTE_ETH_TX_OFFLOAD_IPV4_CKSUM |
                                 RTE_ETH_TX_OFFLOAD_TCP_CKSUM |
                                 RTE_ETH_TX_OFFLOAD_UDP_CKSUM;
    uint64_t tso_offload_flags = RTE_ETH_TX_OFFLOAD_IPV4_CKSUM |
                                 RTE_ETH_TX_OFFLOAD_TCP_CKSUM |
                                 RTE_ETH_TX_OFFLOAD_TCP_TSO;
    uint64_t uso_offload_flags = RTE_ETH_TX_OFFLOAD_IPV4_CKSUM |
                                 RTE_ETH_TX_OFFLOAD_UDP_CKSUM |
                                 RTE_ETH_TX_OFFLOAD_UDP_TSO;
    cksum_supported = (dev_info.tx_offload_capa & cksum_offload_flags) == cksum_offload_flags;
    tso_supported = (dev_info.tx_offload_capa & tso_offload_flags) == tso_offload_flags;
    uso_supported = (dev_info.tx_offload_capa & uso_offload_flags) == uso_offload_flags;

    struct rte_eth_conf conf = {
        .txmode = {
            .offloads = dev_info.tx_offload_capa & tso_offload_flags & uso_offload_flags,
        },
        .intr_conf = {
            .rxq = 1,
        },
    };
    ret = rte_eth_dev_configure(0, 1,1, &conf);
    if (ret != 0) {
        rte_exit(EXIT_FAILURE, "Failed to configure device.");
    }

    struct rte_eth_txconf txconf = dev_info.default_txconf;
    struct rte_eth_rxconf rxconf = dev_info.default_rxconf;

    ret = rte_eth_tx_queue_setup(0, 0, 256, SOCKET_ID_ANY, &txconf);
    if (ret != 0) {
        rte_exit(EXIT_FAILURE, "Failed to setup TX queue.");
    }

    ret = rte_eth_rx_queue_setup(0, 0, 256, SOCKET_ID_ANY, &rxconf, rx_pool);
    if (ret != 0) {
        rte_exit(EXIT_FAILURE, "Failed to setup RX queue.");
    }

    ret = rte_eth_dev_start(0);
    if (ret != 0) {
        rte_exit(EXIT_FAILURE, "Failed to start device.");
    }

    ret = rte_eth_macaddr_get(0, &mac_addr);
    if (ret != 0) {
        rte_exit(EXIT_FAILURE, "Failed to get MAC address.");
    }
}

void init_tap(const char *tapname) {
    int fd = open("/dev/net/tun", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("open(/dev/net/tun)");
        exit(EXIT_FAILURE);
    }

    struct ifreq ifreq = {
        .ifr_name = "vdpdkTap",
        .ifr_flags = IFF_TAP | IFF_NO_PI | IFF_TUN_EXCL | IFF_VNET_HDR,
    };
    if (tapname) {
        size_t tapname_len = strlen(tapname);
        if (tapname_len + 1 > sizeof(ifreq.ifr_name)) {
            printf("TAP name \"%s\" is too long (max length is %zu).\n", tapname, sizeof(ifreq.ifr_name) - 1);
            exit(EXIT_FAILURE);
        }
        memcpy(ifreq.ifr_name, tapname, tapname_len + 1);
    }
    int ret = ioctl(fd, TUNSETIFF, &ifreq);
    if (ret < 0) {
        perror("ioctl TUNSETIFF");
        exit(EXIT_FAILURE);
    }

    int vnethdrsz = HDR_SIZE;
    ret = ioctl(fd, TUNSETVNETHDRSZ, &vnethdrsz);
    if (ret < 0) {
        perror("ioctl TUNSETVNETHDRSZ");
        exit(EXIT_FAILURE);
    }

    unsigned offloads = 0;
    if (cksum_supported) {
        offloads |= TUN_F_CSUM;
        if (tso_supported) {
            offloads |= TUN_F_TSO4 | TUN_F_TSO6;
        }
        if (uso_supported) {
            offloads |= TUN_F_USO4 | TUN_F_USO6;
        }
    }
    ret = ioctl(fd, TUNSETOFFLOAD, offloads);
    if (ret < 0) {
        printf("%d\n", errno);
        perror("ioctl TUNSETOFFLOAD");
        exit(EXIT_FAILURE);
    }

    ifreq.ifr_hwaddr.sa_family = ARPHRD_ETHER;
    memcpy(&ifreq.ifr_hwaddr.sa_data, mac_addr.addr_bytes, 6);
    ret = ioctl(fd, SIOCSIFHWADDR, &ifreq);
    if (ret < 0) {
        perror("ioctl SIOCSIFHWADDR");
        exit(EXIT_FAILURE);
    }

    tap_fd = fd;
}

void poll_tx() {
    while (1) {
        // do_tx will block on reading the tap fd
        do_tx();
    }
}

int poll_rx(void *arg) {
    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0) {
        puts("Failed to create epoll");
        abort();
    }
    int ret = rte_eth_dev_rx_intr_ctl(0, epoll_fd, RTE_INTR_EVENT_ADD, NULL);
    if (ret != 0) {
        puts("Failed to register rx interrupt");
        abort();
    }

    bool wait_mode = false;
    while (1) {
        bool intr_enabled = false;
        if (wait_mode) {
            ret = rte_eth_dev_rx_intr_enable(0, 0);
            if (ret != 0) {
                puts("Failed to enable RX interrupts.");
            } else {
                intr_enabled = true;
            }
        }

        uint16_t nb_rx = do_rx();

        wait_mode = nb_rx == 0;

        if (intr_enabled) {
            if (wait_mode) {
                struct epoll_event dummy;
                ret = epoll_wait(epoll_fd, &dummy, 1, -1);
                if (ret == -1 && errno != EINTR) {
                    puts("Failed to epoll wait.");
                }
            }
            rte_eth_dev_rx_intr_disable(0, 0);
        }
    }
    return 0;
}

void poll_txrx() {
    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0) {
        rte_exit(EXIT_FAILURE, "Failed to create epoll");
    }
    // Add RX to epoll
    int ret = rte_eth_dev_rx_intr_ctl(0, epoll_fd, RTE_INTR_EVENT_ADD, NULL);
    if (ret != 0) {
        rte_exit(EXIT_FAILURE, "Failed to register rx interrupt");
    }
    // Add TX to epoll
    int flags = fcntl(tap_fd, F_GETFL);
    flags |= O_NONBLOCK;
    fcntl(tap_fd, F_SETFL, flags);
    struct epoll_event event = {
        .events = EPOLLIN,
    };
    ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tap_fd, &event);
    if (ret != 0) {
        rte_exit(EXIT_FAILURE, "Failed to register tx epoll");
    }

    bool wait_mode = false;
    while (1) {
        bool intr_enabled = false;
        if (wait_mode) {
            ret = rte_eth_dev_rx_intr_enable(0, 0);
            if (ret != 0) {
                puts("Failed to enable RX interrupts.");
            } else {
                intr_enabled = true;
            }
        }

        do_tx();
        uint16_t nb_rx = do_rx();

        wait_mode = nb_rx == 0;

        if (intr_enabled) {
            if (wait_mode) {
                struct epoll_event dummy;
                ret = epoll_wait(epoll_fd, &dummy, 1, -1);
                if (ret == -1 && errno != EINTR) {
                    puts("Failed to epoll wait.");
                }
            }
            rte_eth_dev_rx_intr_disable(0, 0);
        }
    }
}

void do_tx() {
    struct rte_mbuf *mbuf = rte_pktmbuf_alloc(tx_pool);
    if (!mbuf) {
        return;
    }
    mbuf->data_off = HDR_SIZE;
    ssize_t buf_size = read(tap_fd, mbuf->buf_addr, mbuf->buf_len);
    if (buf_size == -1 && (errno == EAGAIN ||
                           errno == EWOULDBLOCK ||
                           errno == EINTR)) {
        // Try again later
        rte_pktmbuf_free(mbuf);
        return;
    }
    if (buf_size <= HDR_SIZE) {
        rte_exit(EXIT_FAILURE, "tap read failed");
    }
    mbuf->pkt_len = mbuf->data_len = buf_size - HDR_SIZE;

    // printf("TX %zd\n", buf_size);

    struct virtio_net_hdr_v1 *hdr = (struct virtio_net_hdr_v1 *)mbuf->buf_addr;

    if (!(hdr->flags & VIRTIO_NET_HDR_F_NEEDS_CSUM) && hdr->gso_type == VIRTIO_NET_HDR_GSO_NONE) {
        mbuf->ol_flags = 0;
    } else if (hdr->gso_type == VIRTIO_NET_HDR_GSO_TCPV4 || hdr->gso_type == VIRTIO_NET_HDR_GSO_TCPV6) {
        mbuf->l2_len = RTE_ETHER_HDR_LEN;
        mbuf->l3_len = hdr->csum_start - RTE_ETHER_HDR_LEN;
        mbuf->l4_len = hdr->hdr_len - hdr->csum_start;
        mbuf->tso_segsz = hdr->gso_size;
        mbuf->ol_flags = RTE_MBUF_F_TX_TCP_SEG;
        if (hdr->gso_type == VIRTIO_NET_HDR_GSO_TCPV4) {
            mbuf->ol_flags |= RTE_MBUF_F_TX_IPV4 | RTE_MBUF_F_TX_IP_CKSUM;
        } else {
            mbuf->ol_flags |= RTE_MBUF_F_TX_IPV6;
        }

        if (mbuf->l2_len + mbuf->l3_len + mbuf->l4_len != hdr->hdr_len || hdr->csum_offset != 16) {
            printf("Missmatched TCP TSO header lengths.");
            rte_pktmbuf_free(mbuf);
            return;
        }

        struct rte_ipv4_hdr *ipv4_hdr;
        struct rte_tcp_hdr *tcp_hdr;
        ipv4_hdr = rte_pktmbuf_mtod_offset(mbuf, struct rte_ipv4_hdr *, mbuf->l2_len);
        tcp_hdr = rte_pktmbuf_mtod_offset(mbuf, struct rte_tcp_hdr *, mbuf->l2_len + mbuf->l3_len);
        ipv4_hdr->hdr_checksum = 0;
        tcp_hdr->cksum = rte_ipv4_phdr_cksum(ipv4_hdr, mbuf->ol_flags);

        // printf("GSO read_size %zd, pkt_len %u, l2 %u, l3 %u, l4 %u, segsz %u\n",
        //        buf_size, mbuf->pkt_len, mbuf->l2_len, mbuf->l3_len, mbuf->l4_len, mbuf->tso_segsz);
    } else if (hdr->gso_type == VIRTIO_NET_HDR_GSO_UDP_L4) {
        // This path is pretty much untested due to lack of driver support
        mbuf->l2_len = RTE_ETHER_HDR_LEN;
        mbuf->l3_len = hdr->csum_start - RTE_ETHER_HDR_LEN;
        mbuf->l4_len = hdr->hdr_len - hdr->csum_start;
        mbuf->tso_segsz = hdr->gso_size;
        mbuf->ol_flags = RTE_MBUF_F_TX_UDP_SEG;

        struct rte_ether_hdr *ether_hdr = rte_pktmbuf_mtod(mbuf, struct rte_ether_hdr *);
        uint16_t ethertype = rte_be_to_cpu_16(ether_hdr->ether_type);
        if (ethertype == RTE_ETHER_TYPE_IPV4) {
            mbuf->ol_flags |= RTE_MBUF_F_TX_IPV4 | RTE_MBUF_F_TX_IP_CKSUM;
        } else if (ethertype == RTE_ETHER_TYPE_IPV6) {
            mbuf->ol_flags |= RTE_MBUF_F_TX_IPV6;
        }

        if (mbuf->l2_len + mbuf->l3_len + mbuf->l4_len != hdr->hdr_len || hdr->csum_offset != 6) {
            printf("Missmatched UDP USO header lengths.");
            rte_pktmbuf_free(mbuf);
            return;
        }
    } else if (hdr->flags & VIRTIO_NET_HDR_F_NEEDS_CSUM) {
        mbuf->l2_len = RTE_ETHER_HDR_LEN;
        mbuf->l3_len = hdr->csum_start - RTE_ETHER_HDR_LEN;

        struct rte_ether_hdr *ether_hdr = rte_pktmbuf_mtod(mbuf, struct rte_ether_hdr *);
        uint16_t ethertype = rte_be_to_cpu_16(ether_hdr->ether_type);
        if (ethertype == RTE_ETHER_TYPE_IPV4) {
            mbuf->ol_flags = RTE_MBUF_F_TX_IPV4;
        } else if (ethertype == RTE_ETHER_TYPE_IPV6) {
            mbuf->ol_flags = RTE_MBUF_F_TX_IPV6;
        } else {
            mbuf->ol_flags = 0;
        }

        if (hdr->csum_offset == 16) {
            mbuf->ol_flags |= RTE_MBUF_F_TX_TCP_CKSUM;
        } else if (hdr->csum_offset == 6) {
            mbuf->ol_flags |= RTE_MBUF_F_TX_UDP_CKSUM;
        } else {
            printf("Invalid csum_offset.\n");
            rte_pktmbuf_free(mbuf);
            return;
        }
    } else {
        printf("Unexpected packet\n");
        rte_pktmbuf_free(mbuf);
        return;
    }

    uint16_t nb_tx = rte_eth_tx_burst(0, 0, &mbuf, 1);
    if (nb_tx < 1) {
        rte_pktmbuf_free(mbuf);
    }
}

uint16_t do_rx() {
    enum {MAX_MBUFS = 32};
    struct rte_mbuf *mbufs[MAX_MBUFS];

    uint16_t nb_rx = rte_eth_rx_burst(0, 0, mbufs, MAX_MBUFS);
    for (uint16_t i = 0; i < nb_rx; i++) {
        struct rte_mbuf *mbuf = mbufs[i];

        if (mbuf->nb_segs != 1) {
            printf("Unsupported multi-segment RX\n");
            rte_pktmbuf_free(mbuf);
            continue;
        }

        struct virtio_net_hdr_v1 *hdr = rte_pktmbuf_mtod_offset(mbuf, struct virtio_net_hdr_v1 *, -HDR_SIZE);
        memset(hdr, 0, HDR_SIZE);
        // printf("RX\n");
        write(tap_fd, hdr, HDR_SIZE + (size_t)mbuf->data_len);
        rte_pktmbuf_free(mbuf);
    }
    return nb_rx;
}
