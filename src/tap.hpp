#pragma once

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
  // flags and proto seem only to be used witout IFF_NO_PI
  uint16_t flags;
  uint16_t proto;
  char buf[TAP_ETH_FRAME_MAX]; // actual ethernet packet
};

class Tap {
public:
  static const int MAX_BUF = TAP_ETH_FRAME_MAX;

  char ifName[IFNAMSIZ];
  int fd;
  char rxFrame[MAX_BUF];
  size_t rxFrame_buf_used; // how much frame->buf is actually filled with data
  char txFrame[MAX_BUF];
  size_t txFrame_used; // frame.buf may not be fully used. This variable describes how much of the entire frame needs to be copied to catch all of buf.
  size_t i = offsetof(tap_eth_frame, buf);

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

  void send(const char *buf, const size_t len) {
    if (len > Tap::MAX_BUF) die("Attempting to send a packet too large for vmux (%zu)", len);
    memcpy(&(this->txFrame), (void*)buf, len);
    size_t n = write(this->fd, &(this->txFrame), len);
    if (n != len) {
      die("Could not send full packet (sent %zu of %zu b). Is the tap interface down?", n, len);
    }
  }

  void recv() {
    size_t n = read(this->fd, &(this->rxFrame), Tap::MAX_BUF);
    this->rxFrame_buf_used = n;
    printf("recv %zu bytes\n", n);
    Util::dump_pkt(&this->rxFrame, this->rxFrame_buf_used);
    if (n < 0)
      die("could not read from tap");
  }

  void dumpRx() {
    while (true) {
      this->recv();
      Util::dump_pkt(this->rxFrame, this->rxFrame_buf_used);
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
