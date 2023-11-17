#include "util.hpp"
#include <cstring>
#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>

#define TAP_ETH_FRAME_MAX 9000

// this is what linux tuntap gives us on read()
struct __attribute__ ((packed)) tap_eth_frame {
  uint16_t flags;
  uint16_t proto;
  char buf[TAP_ETH_FRAME_MAX]; // actual ethernet packet
};

class Tap {
public:
  static const int MAX_BUF = TAP_ETH_FRAME_MAX;

  char ifName[IFNAMSIZ];
  int fd;
  struct tap_eth_frame frame;
  size_t frame_buf_used; // how much frame->buf is actually filled with data

  ~Tap() {
    close(this->fd);
    // TODO remove Tap from epoll
  }

  int open_tap(const char *dev) {
    struct ifreq ifr;
    int fd, err;

    if ((fd = open("/dev/net/tun", O_RDWR)) < 0) // TODO noblock plz
      die("Cannot open /dev/net/tun");

    memset(&ifr, 0, sizeof(ifr));

    /* Flags: IFF_TUN   - TUN device (no Ethernet headers)
     *        IFF_TAP   - TAP device
     *
     *        IFF_NO_PI - Do not provide packet information
     */
    ifr.ifr_flags = IFF_TAP;
    if (*dev)
      strncpy(ifr.ifr_name, dev, IFNAMSIZ);

    if ((err = ioctl(fd, TUNSETIFF, (void *)&ifr)) < 0) {
      close(fd);
      return err;
    }
    strcpy(this->ifName, ifr.ifr_name);
    this->fd = fd;
    return 0;
  }

  void recv() {
    size_t n = read(this->fd, &(this->frame), Tap::MAX_BUF);
    this->frame_buf_used = n;
    printf("recv %zu bytes\n", n);
    Util::dump_pkt(&this->frame.buf, this->frame_buf_used);
    if (n < 0)
      die("could not read from tap");
  }

  void dumpRx() {
    while (true) {
      this->recv();
      Util::dump_pkt(this->frame.buf, this->frame_buf_used);
    }
  }

  void registerEpoll(int efd) {
    struct epoll_event e;
    e.events = EPOLLIN;
    e.data.u64 = 1337;

    if (0 != epoll_ctl(efd, EPOLL_CTL_ADD, this->fd, &e))
      die("could not register tap fd to epoll");
  }
};
