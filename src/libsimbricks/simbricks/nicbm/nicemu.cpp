
#include <src/libsimbricks/simbricks/nicbm/nicbm.h>

namespace nicbm {
  typedef Runner::Device Device;

  /**
   * Replaces the Runner class. Adapts simbricks behavioral models to an libvfio-user-ish interface. 
   **/
  class Emulator {
    protected:
      Device *device_;

    public:
      Emulator(Device &dev) : device_(&dev) {}
  };
};
