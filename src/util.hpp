
#pragma once

#include <stdexcept>
#include <err.h>
#include <dirent.h>
#include <vector> 
#include <cstring>

// as per PCI spec, there can be at most 2048 MSIx inerrupts per device
#define PCI_MSIX_MAX 2048

// exit() and err() breaks invariants for RAII (destructors). Therefore we use
// warn() instead to printf an error and throw afterwards to exit.
#define die(...) { \
  warn(__VA_ARGS__); \
  throw std::runtime_error("See error above"); \
  }


std::string get_iommu_group(std::string pci_device);
std::vector<int>  get_hardware_ids(std::string pci_device,std::string iommu_group);
