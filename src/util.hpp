#pragma once

#include "libvfio-user.h"
#include "src/libsimbricks/simbricks/pcie/proto.h"
#include <cstring>
#include <dirent.h>
#include <err.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/param.h>
#include <vector>
#include <limits>

// as per PCI spec, there can be at most 2048 MSIx inerrupts per device
#define PCI_MSIX_MAX 2048

// exit() and err() breaks invariants for RAII (destructors). Therefore we use
// warn() instead to printf an error and throw afterwards to exit.
#define die(...)                                                               \
  {                                                                            \
    warn(__VA_ARGS__);                                                         \
    throw std::runtime_error("See error above");                               \
  }

struct epoll_callback {
  int fd;    // passed as arg1 to callback
  void *ctx; // passed as arg2 to callback
  void (*callback)(int, void *);
};

static int LOG_LEVEL = LOG_DEBUG;

// __builtin_expect(!! ... to replace [[unlikely]] which is unsupported on clang
#define if_log_level(level, expr) \
  do { \
    if (__builtin_expect(!!(LOG_LEVEL >= level), 0)) { \
      expr; \
    } \
  } while (0)

class Util {
public:
  static std::string get_iommu_group(std::string pci_device) {
    std::string path = "/sys/kernel/iommu_groups/";
    struct dirent *iommu_group;
    DIR *iommu_dir = opendir(path.c_str());
    if (iommu_dir == NULL) {
      return "";
    }
    while ((iommu_group = readdir(iommu_dir)) != NULL) {
      if (strcmp(iommu_group->d_name, ".") != 0 &&
          strcmp(iommu_group->d_name, "..") != 0) {
        std::string iommu_group_str = iommu_group->d_name;
        struct dirent *iommu_group_dir;
        DIR *pci = opendir((path + iommu_group->d_name + "/devices").c_str());
        while ((iommu_group_dir = readdir(pci)) != NULL) {
          if (pci_device == iommu_group_dir->d_name) {
            closedir(pci);
            closedir(iommu_dir);
            return iommu_group_str;
          }
        }
        closedir(pci);
      }
    }
    closedir(iommu_dir);
    return "";
  }

  static std::vector<int> get_hardware_ids(std::string pci_device,
                                           std::string iommu_group) {
    std::string path = "/sys/kernel/iommu_groups/" + iommu_group + "/devices/" +
                       pci_device + "/";
    std::vector<std::string> values = {"revision", "vendor", "device",
                                       "subsystem_vendor", "subsystem_device"};
    std::vector<int> result;
    int bytes_read;
    char id_buffer[7] = {0};
    FILE *id;

    for (size_t i = 0; i < values.size(); i++) {
      id = fopen((path + values[i]).c_str(), "r");
      if (id == NULL) {
        result.clear();
        printf("Failed to open iommu sysfs file: %s\n",
               (path + values[i]).c_str());
        return result;
      }
      bytes_read =
          fread(id_buffer, 1, sizeof(id_buffer) / sizeof(id_buffer[0]) - 1, id);
      if (bytes_read < 1) {
        result.clear();
        printf("Failed to read %s, got %s\n", values[i].c_str(), id_buffer);
        return result;
      }
      result.push_back((int)strtol(id_buffer, NULL, 0));
      fclose(id);
    }

    return result;
  }

  /* convert simbricks bar flags (SIMBRICKS_PROTO_PCIE_BAR_*) to vfio-user flags
   * (VFU_REGION_FLAG_*) */
  static int convert_flags(int bricks) {
    int vfu = 0;

    // if BAR_IO (port io) is not set, it is FLAG_MEM (MMIO)
    if (!(bricks & SIMBRICKS_PROTO_PCIE_BAR_IO)) {
      vfu |= VFU_REGION_FLAG_MEM;
    }

    return vfu;
  }

  // dump an ethernet packet
  static void dump_pkt(void *buffer, size_t len) {
    ethhdr *eth = (ethhdr *)malloc(sizeof(ethhdr));
    memcpy(eth, buffer, MIN(len, sizeof(*eth)));

    printf("src=%02X:%02X:%02X:%02X:%02X:%02X\n", eth->h_source[5],
           eth->h_source[4], eth->h_source[3], eth->h_source[2],
           eth->h_source[1], eth->h_source[0]);

    printf("dst=%02X:%02X:%02X:%02X:%02X:%02X\n", eth->h_dest[5],
           eth->h_dest[4], eth->h_dest[3], eth->h_dest[2], eth->h_dest[1],
           eth->h_dest[0]);
    printf("proto=%04X\n", ntohs(eth->h_proto));
  }

  static bool ts_before(const struct timespec* a, const struct timespec* b) {
    if (a->tv_sec > b->tv_sec) {
      return false;
    } else {
      return (a->tv_sec < b->tv_sec || a->tv_nsec < b->tv_nsec);
    }
  }

  // approx. diff in nsec
  static ulong ts_diff(const struct timespec *time1, const struct timespec *time0) {
    struct timespec diff = {.tv_sec = time1->tv_sec - time0->tv_sec, //
        .tv_nsec = time1->tv_nsec - time0->tv_nsec};
    if (diff.tv_nsec < 0) {
      // diff.tv_nsec += 1000000000; // nsec/sec
      // diff.tv_sec--;
      diff.tv_nsec = std::numeric_limits<long>::max();
    }
    return diff.tv_nsec;
  }
  
  static ulong ulong_diff(const ulong x, const ulong y) {
    return x > y ? x - y : y - x;
  }

  static ulong ulong_min(const ulong x, const ulong y) {
    return x < y ? x : y;
  }
};
