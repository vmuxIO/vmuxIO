// l2 latency measurement based on kernel network interfaces
// compile with: clang++ test/timer.cpp -o timer -O3

#include <cstdio>
#include <cstring>
#include <ctime>
#include <netinet/in.h>
#include <numeric>
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/ether.h>
#include <linux/if_packet.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>


int main(int argc, char* argv[]) {
  // parse args

  if (argc != 3) {
    printf("Usage: %s <Destination MAC> <Interface>\n", argv[0]);
    return 1;
  }

  unsigned char dst[6];
  std::string if_name = argv[2];
  int runtime_s = 10;

  sscanf(argv[1], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &dst[0], &dst[1], &dst[2], &dst[3], &dst[4], &dst[5]);

  std::vector<uint64_t> latencies_ns;

  int fd;
  if ((fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1) {
    perror("socket");
  }

  struct ifreq if_idx;
  memset(&if_idx, 0, sizeof(struct ifreq));
  strncpy(if_idx.ifr_name, if_name.c_str(), IFNAMSIZ-1);
  if (ioctl(fd, SIOCGIFINDEX, &if_idx) < 0)
    perror("SIOCGIFINDEX");

  struct ifreq if_mac;
  memset(&if_mac, 0, sizeof(struct ifreq));
  strncpy(if_mac.ifr_name, if_name.c_str(), IFNAMSIZ-1);
  if (ioctl(fd, SIOCGIFHWADDR, &if_mac) < 0)
    perror("SIOCGIFHWADDR");

  int tx_len = 0;
  int ether_type = htons(0x1234);
  char sendbuf[1024];
  char recvbuf[1024];
  struct ether_header *eh = (struct ether_header *) sendbuf;
  struct ether_header *reh = (struct ether_header *) recvbuf;
  memset(sendbuf, 0, 1024);
  memset(recvbuf, 0, 1024);
  /* Ethernet header */
  eh->ether_shost[0] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[0];
  eh->ether_shost[1] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[1];
  eh->ether_shost[2] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[2];
  eh->ether_shost[3] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[3];
  eh->ether_shost[4] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[4];
  eh->ether_shost[5] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[5];
  eh->ether_dhost[0] = dst[0];
  eh->ether_dhost[1] = dst[1];
  eh->ether_dhost[2] = dst[2];
  eh->ether_dhost[3] = dst[3];
  eh->ether_dhost[4] = dst[4];
  eh->ether_dhost[5] = dst[5];
  eh->ether_type = ether_type;
  tx_len += sizeof(struct ether_header);

  tx_len = 64;

  /* Destination address */
  struct sockaddr_ll socket_address;
  /* Index of the network device */
  socket_address.sll_ifindex = if_idx.ifr_ifindex;
  /* Address length*/
  socket_address.sll_halen = ETH_ALEN;
  /* Destination MAC */
  socket_address.sll_addr[0] = dst[0];
  socket_address.sll_addr[1] = dst[1];
  socket_address.sll_addr[2] = dst[2];
  socket_address.sll_addr[3] = dst[3];
  socket_address.sll_addr[4] = dst[4];
  socket_address.sll_addr[5] = dst[5];

  for(int i = 0; i < runtime_s * 1000; i++) {
    /* Send packet */
    if (sendto(fd, sendbuf, tx_len, 0, (struct sockaddr*)&socket_address, sizeof(struct sockaddr_ll)) < 0) {
      printf("Send failed\n");
      continue;
    }
    struct timespec tsa;
    clock_gettime(CLOCK_MONOTONIC, &tsa);

    socklen_t addr_len;
    while(true) {
      int rec = recv(fd, recvbuf, 1024, 0);

      // TODO compare packets properly
      if (rec != 64 || reh->ether_type != ether_type) {
        // printf("wrong packet! %d, %x\n", rec, reh->ether_type);
        continue;
      } else {
        // correct packet
        break;
      }
    }

    struct timespec tsb;
    clock_gettime(CLOCK_MONOTONIC, &tsb);

    if (tsa.tv_sec != tsb.tv_sec) {
      // printf("second changed!\n");
      continue;
    }
    uint64_t diff_ns = tsb.tv_nsec - tsa.tv_nsec;

    if (i % 1000 == 0)
      printf("latency ns: %ld\n", diff_ns);

    latencies_ns.push_back(diff_ns);
    usleep(100);
  }

  double average = std::accumulate(latencies_ns.begin(), latencies_ns.end(), 0.0) / latencies_ns.size();
  printf("avg ns: %.0f\n", average);

  return 0;
}
