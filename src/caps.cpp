#include "util.hpp"
#include "caps.hpp"
#include "string.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <stdio.h>

extern "C" {
  #include "libvfio-user.h"
  #include "subprojects/libvfio-user/lib/private.h"
}

static void
_log([[maybe_unused]] vfu_ctx_t *vfu_ctx, [[maybe_unused]] int level, char const *msg)
{
    fprintf(stderr, "capabilies[%d]: %s\n", getpid(), msg);
}

void Capabilities::map_header() {
  std::string config_path = "/sys/bus/pci/devices/0000:18:00.0/config"; // TODO dont hardcode id
  //int fd = open(config_path.c_str(), O_RDONLY);
  FILE *fd = fopen(config_path.c_str(), "rb");
  if (fd == NULL)
    die("Cannot open %s", config_path.c_str());
  this->header_size = 4096;// TODO dont hardcode size of this file
  //this->header_mmap = mmap(NULL, this->header_size, PROT_READ, MAP_PRIVATE, fd, 0);
  //if (this->header_mmap == MAP_FAILED)
    //die("mmap failed");
  this->header_mmap = malloc(this->header_size);
  if (this->header_mmap == NULL)
    die("malloc failed");

  size_t ret = fread(this->header_mmap, sizeof(char), this->header_size, fd);
  if (ret != this->header_size)
    die("only %zu bytes read", ret);
}

Capabilities::Capabilities(const vfio_region_info *config_info, void *config_ptr) {
  (void)(config_ptr); // unused. TODO
  this->vfu_ctx_stub = (vfu_ctx_t*) malloc(sizeof(vfu_ctx_t));
  if (this->vfu_ctx_stub == NULL)
    die("malloc failed");
  memset(this->vfu_ctx_stub, 0, sizeof(vfu_ctx_t));

  this->vfu_ctx_stub->reg_info = (vfu_reg_info_t*)malloc(VFU_PCI_DEV_NUM_REGIONS * sizeof(vfu_reg_info_t));
  if (this->vfu_ctx_stub->reg_info == NULL)
    die("malloc failed");
  memset(this->vfu_ctx_stub->reg_info, 0, VFU_PCI_DEV_NUM_REGIONS * sizeof(vfu_reg_info_t));

  int ret = vfu_setup_log(this->vfu_ctx_stub, _log, LOG_DEBUG);
  if (ret < 0) {
    die("failed to setup log");
  }

  this->map_header();

  //this->vfu_ctx_stub->pci.config_space = (vfu_pci_config_space_t *)config_ptr;
  this->vfu_ctx_stub->pci.config_space = (vfu_pci_config_space_t *)this->header_mmap;

  vfu_reg_info_t *config_info_stub = &(this->vfu_ctx_stub->reg_info[VFU_PCI_DEV_CFG_REGION_IDX]);
  memset(config_info_stub, 0, sizeof(vfu_reg_info_t));
  config_info_stub->size = config_info->size;
  config_info_stub->offset = config_info->offset;

  if (config_info_stub->size != this->header_size)
    die("Inconsistent pci config space size found");

}

// returns void pointer cap_data and writes cap_size
void *Capabilities::capa(const char name[], int id, size_t size, bool extended) {
  // TODO error handling
  size_t cap_offset = vfu_pci_find_next_capability(this->vfu_ctx_stub, extended, 0, id);
  //size_t cap_size = cap_size( vfu_ctx, data, extended )
  size_t cap_size = size;
  void *cap_data = malloc(cap_size);
  // TODO error handling
  memcpy(cap_data, (char*)this->header_mmap + cap_offset, cap_size);
  printf("%s capability at offset %zu\n", name, cap_offset);
  return cap_data;
};

void *Capabilities::dsn() {
  return this->capa("device serial number (ext)", PCI_EXT_CAP_ID_DSN, PCI_EXT_CAP_DSN_SIZEOF, true);
};

void *Capabilities::pm() {
  return this->capa("power management", PCI_CAP_ID_PM, PCI_PM_SIZEOF, false);
};

void *Capabilities::msi() {
  return this->capa("msi", PCI_CAP_ID_MSI, PCI_CAP_MSIX_SIZEOF, false); // TODO can be up to 0x18 long as per spec sec 7.7.1
};

void *Capabilities::msix() {
  return this->capa("msix", PCI_CAP_ID_MSIX, PCI_CAP_MSIX_SIZEOF, false); // TODO we dont copy tables here
};

void *Capabilities::exp() {
  return this->capa("PCI Express (spec sec 6.5.3)", PCI_CAP_ID_EXP, 0x34, false); // slot registers at 0x34, ... and 0x3a are reserved
};

void *Capabilities::vpd() {
  return this->capa("vital product data", PCI_CAP_ID_VPD, PCI_CAP_VPD_SIZEOF, false);
};
