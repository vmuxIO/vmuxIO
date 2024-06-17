#include <sys/syslog.h>
#include "util.hpp"
#include "libvfio-user.h"
#include "src/libsimbricks/simbricks/pcie/proto.h"

int LOG_LEVEL = LOG_DEBUG;

#include <boost/throw_exception.hpp>
void boost::throw_exception(std::exception const & e, boost::source_location const& src){
  die("%s: %s\n", src.to_string().c_str(), e.what());
}


/* convert simbricks bar flags (SIMBRICKS_PROTO_PCIE_BAR_*) to vfio-user flags
 * (VFU_REGION_FLAG_*) */
int Util::convert_flags(int bricks) {
  int vfu = 0;

  // if BAR_IO (port io) is not set, it is FLAG_MEM (MMIO)
  if (!(bricks & SIMBRICKS_PROTO_PCIE_BAR_IO)) {
    vfu |= VFU_REGION_FLAG_MEM;
  }

  return vfu;
}
