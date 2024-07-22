#pragma once

#include "util.hpp"
#include <cstring>
#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include "src/drivers/driver.hpp"

class Tap : public Driver {
public:

  char ifName[IFNAMSIZ];

  Tap() {
    this->alloc_rx_lists(1, 1);
    this->alloc_rx_bufs();
  }

  virtual ~Tap() {
    close(this->fd); // does onthing if uninitialized (== 0)
  }

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
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
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

  void send(int vm_id, const char *buf, const size_t len) {
    if (len > Tap::MAX_BUF)
      die("Attempting to send a packet too large for vmux (%zu)", len);
    memcpy(&(this->txFrame), (void *)buf, len);
    size_t n = write(this->fd, &(this->txFrame), len);
    if (n != len) {
      die("Could not send full packet (sent %zu of %zu b). Is the tap "
          "interface down?",
          n, len);
    }
  }

  void recv(int _vm_number) {
    size_t n = read(this->fd, this->rxBufs[0], Tap::MAX_BUF);
    this->rxBuf_used[0] = n;
    this->nb_bufs_used[0] = 1;
    if (LOG_LEVEL >= LOG_DEBUG) {
      printf("recv %zu bytes\n", n);
      Util::dump_pkt(this->rxBufs[0], this->rxBuf_used[0]);
    }
    if (n < 0)
      die("could not read from tap");
  }

  virtual void recv_consumed(int _vm_number) {
    this->nb_bufs_used[0] = 0;
  }

  void dumpRx() {
    while (true) {
      this->recv(0);
      Util::dump_pkt(this->rxBufs[0], this->rxBuf_used[0]);
    }
  }
};
