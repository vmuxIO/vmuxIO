#include "util.hpp"
#include <cstring>
#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <sys/ioctl.h>


class Tap {
public:
  static const int MAX_BUF = 9000;

  char ifName[IFNAMSIZ];
  int fd;
  char buf[Tap::MAX_BUF];

  int open_tap(const char *dev) {
    struct ifreq ifr;
    int fd, err;

    if ((fd = open("/dev/net/tun", O_RDWR)) < 0)
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
    size_t n = read(this->fd, this->buf, Tap::MAX_BUF);
    printf("recv %zu bytes\n", n);
    if (n < 0)
      die("could not read from tap");
  }

  void dumpRx() {
    while (true) {
      this->recv();
      Util::dump_pkt(this->buf, Tap::MAX_BUF);
    }
  }
};
