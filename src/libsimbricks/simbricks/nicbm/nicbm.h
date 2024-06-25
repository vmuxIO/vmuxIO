/*
 * Copyright 2021 Max Planck Institute for Software Systems, and
 * National University of Singapore
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SIMBRICKS_NICBM_NICBM_H_
#define SIMBRICKS_NICBM_NICBM_H_

#include <cassert>
#include <cstring>
#include <deque>
#include <set>
#include "interrupts/none.hpp"
#include "interrupts/simbricks.hpp"
#include "vfio-server.hpp"
#include "devices/vmux-device.hpp"
#include "util.hpp"
#include <memory>

#include <src/libsimbricks/simbricks/base/cxxatomicfix.h>
extern "C" {
#include <src/libsimbricks/simbricks/nicif/nicif.h>
}

namespace nicbm {

static const size_t kMaxDmaLen = 2048;

class DMAOp {
 public:
  virtual ~DMAOp() = default;
  bool write_;
  uint64_t dma_addr_;
  size_t len_;
  void *data_;
};

class TimedEvent {
 public:
  TimedEvent() : time_(0), priority_(0) {
  }
  virtual ~TimedEvent() = default;
  uint64_t time_;
  int priority_;
};

/**
 * The Runner drives the main simulation loop. It's initialized with a reference
 * to a device it should manage, and then once `runMain` is called, it will
 * start interacting with the PCI and Ethernet queue and forwarding calls to the
 * device as needed.
 * */
class Runner {
 public:

  class Device;
  class CallbackAdaptor;

  /**
   * Replaces the Runner class. Adapts behavioral models from simbricks or other libraries to an libvfio-user-ish interface. 
   * From the point of view of emulators interfacing directly with libvfio-user, this class is similar to "device context".
   **/
  class DeviceEmulator {
    protected:
      // behavioral model of a device
      Device *device_;

    public:
      DeviceEmulator(Device &dev);

      // Functions to be called through libvfio-user by the VM
      // In rust speak they would belong to "trait MmioProvider"
      void RegRead(uint8_t bar, uint64_t addr, void *dest,
                           size_t len) {
        device_->RegRead(bar, addr, dest, len);
      }
      void RegWrite(uint8_t bar, uint64_t addr, const void *src,
                            size_t len) {
        device_->RegWrite(bar, addr, src, len);
      }

      // Functions to be called by the owner of DeviceEmulator. The device must have these functions as well.

      // when calling this, the  device will do all pending DMAs.
      void DmaComplete(DMAOp &op);
      // send a packet from the network fabric to the NIC device. Optional destination queue hint.
      void EthRx(uint8_t port, std::optional<uint16_t> queue, const void *data, size_t len);

      // Functions to be called by the Device
      // IssueDma, MsiXIssue, EthSend, EventSchedule, ...
      // In rust these would belong to "trait DeviceCallbacks" or "struct DeviceContext"

      // device tells the Emulator to issue a DMA. The emulator can then do the memcopy or sgl_read/write.
      void IssueDma(DMAOp &op);
      // Inject interrupts (maybe replace with a single function?)
      // Similar to DeviceContext::trigger_irq
      void MsiIssue(uint8_t vec);
      void MsiXIssue(uint8_t vec);
      void IntXIssue(bool level);
      void EthSend(const void *data, size_t len);

      // Further methods called by the Device to set up passthrough or mediation (can be ignored for now)
      
      // true: phy. device and emu. device can dma
      void MapPhysicalDeviceDma(bool enable); 
      // returns where registers of a physical device have been mapped to. Vmux can now use them.
      void MapPhysicalDeviceRegistersToVmux(bool enable); 
      // true: RegRead callbacks stop and are redirected to physical device
      void MapPhysicalDeviceRegistersToVm(bool enable); 

      // Further methods called by the Device to do multiple emulated devices (backed by one physical one)
      // ...
  };

  class Device {
   public:
    Runner *runner_; // vmux leaves this at null and replaces it with CallbackAdaptor
    std::shared_ptr<CallbackAdaptor> vmux;

   protected:
    bool int_intx_en_;
    bool int_msi_en_;
    bool int_msix_en_;

   public:
    /**
     * Initialize device specific parameters (pci dev/vendor id,
     * BARs etc. in intro struct.
     */
    virtual void SetupIntro(struct SimbricksProtoPcieDevIntro &di) = 0;

    /**
     * execute a register read from `bar`:`addr` of length `len`.
     * Should store result in `dest`.
     */
    virtual void RegRead(uint8_t bar, uint64_t addr, void *dest,
                         size_t len) = 0;

    /**
     * execute a register write to `bar`:`addr` of length `len`,
     * with the data in `src`.
     */
    virtual void RegWrite(uint8_t bar, uint64_t addr, const void *src,
                          size_t len) = 0;

    /**
     * the previously issued DMA operation `op` completed.
     */
    virtual void DmaComplete(DMAOp &op) = 0;

    /**
     * A packet has arrived on the wire, of length `len` with
     * payload `data`. Optional destination queue hint.
     */
    virtual void EthRx(uint8_t port, std::optional<uint16_t> queue, const void *data, size_t len) = 0;

    /**
     * A timed event is due.
     */
    virtual void Timed(TimedEvent &te);

    /**
     * Device control update
     */
    virtual void DevctrlUpdate(struct SimbricksProtoPcieH2DDevctrl &devctrl);
  };

/*
 * Adaptor to replace the simbricks class Runner
 * Impl in e810.hpp.
 */
class CallbackAdaptor {
  private:
    const uint8_t (*mac_addr)[6];
  public:
    std::shared_ptr<VfioUserServer> vfu; // must be lazily set during VmuxDevice.setup_vfu()
    std::shared_ptr<Device> model; 
    std::shared_ptr<VmuxDevice> device;
    std::vector<std::shared_ptr<InterruptThrottlerSimbricks>> irqThrottle;

    CallbackAdaptor(std::shared_ptr<VmuxDevice> device, const uint8_t (*mac_addr)[6], std::vector<std::shared_ptr<InterruptThrottlerSimbricks>> irqThrottle) : mac_addr(mac_addr), device(device), irqThrottle(irqThrottle) {}

    /* these three are for `Runner::Device`. */
    void IssueDma(nicbm::DMAOp &op) {
      if_log_level(LOG_DEBUG, 
        printf("CallbackAdaptor::IssueDma: read %d, addr %lx, len %zu\n", !op.write_, op.dma_addr_, op.len_)
      );
      // __builtin_dump_struct(&op, &printf); // dump_struct doesnt work on classes
      void *local_addr = this->vfu->dma_local_addr(op.dma_addr_, op.len_);
      if (!local_addr) {
        die("Could not translate DMA address");
      }
      if (op.write_) {
        memcpy(local_addr, op.data_, op.len_);
      } else {
        memcpy(op.data_, local_addr, op.len_);
      }
      model->DmaComplete(op);
    }
    void MsiIssue(uint8_t vec) {
      printf("CallbackAdaptor::MsiIssue(%d)\n", vec);
      die("not implemented");
    }
    void MsiXIssue(uint8_t vec, uint64_t mindelay) {
      // E1000EmulatedDevice *this_ = (E1000EmulatedDevice *)private_ptr;
      // spacing_s = 1 / ( 10^9 / (reg * 256) )
      // spcaing_s = (reg * 256) / 10^9
      // ulong spacing_us = (e1000_interrupt_throtteling_reg(this_->e1000, -1) * 256);
      this->irqThrottle[vec]->try_interrupt(mindelay, false);

      int ret = 0;
      // ret = vfu_irq_trigger(this->vfu->vfu_ctx, vec);
      // if (ret != 0)
      //   die("E810: could not send interrupt");
      if_log_level(LOG_DEBUG, printf("CallbackAdaptor::MsiXIssue: Triggered interrupt. ret = %d, errno: %d\n", ret, errno));
    }
    void IntXIssue(bool level) {
      printf("CallbackAdaptor::IntXIssue(%d)\n", level);
      die("not implemented");
    }
    void EthSend(const void *data, size_t len) {
      if_log_level(LOG_DEBUG, 
        printf("CallbackAdaptor::EthSend(len=%zu)\n", len)
      );
      this->device->driver->send(this->device->device_id, (char*)data, len);
    }

    void EventSchedule(nicbm::TimedEvent &evt) {
      printf("CallbackAdaptor::EventSchedule\n");
      die("not implemented");
    }
    void EventCancel(nicbm::TimedEvent &evt) {
      printf("CallbackAdaptor::EventCancel\n");
      die("not implemented");
    }

    uint64_t TimePs() const {
     die("unimplemented");
     return 0;
    }
    uint64_t GetMacAddr() const {
     uint64_t mac = 0;
     memcpy(&mac, this->mac_addr, sizeof(*this->mac_addr));
     return mac;
    }
};



 protected:
  struct EventCmp {
    bool operator()(TimedEvent *a, TimedEvent *b) const {
      return a->time_ < b->time_ ||
             (a->time_ == b->time_ && a->priority_ < b->priority_);
    }
  };

  uint64_t main_time_;
  Device &dev_;
  std::multiset<TimedEvent *, EventCmp> events_;
  std::deque<DMAOp *> dma_queue_;
  size_t dma_pending_;
  uint64_t mac_addr_;
  struct SimbricksBaseIfParams pcieParams_;
  struct SimbricksBaseIfParams netParams_;
  const char *shmPath_;
  struct SimbricksNicIf nicif_;
  struct SimbricksProtoPcieDevIntro dintro_;

  volatile union SimbricksProtoPcieD2H *D2HAlloc();
  volatile union SimbricksProtoNetMsg *D2NAlloc();

  void H2DRead(volatile struct SimbricksProtoPcieH2DRead *read);
  void H2DWrite(volatile struct SimbricksProtoPcieH2DWrite *write);
  void H2DReadcomp(volatile struct SimbricksProtoPcieH2DReadcomp *rc);
  void H2DWritecomp(volatile struct SimbricksProtoPcieH2DWritecomp *wc);
  void H2DDevctrl(volatile struct SimbricksProtoPcieH2DDevctrl *dc);
  void PollH2D();

  void EthRecv(volatile struct SimbricksProtoNetMsgPacket *packetl);
  void PollN2D();

  bool EventNext(uint64_t &retval);
  void EventTrigger();

  void DmaDo(DMAOp &op);
  void DmaTrigger();

  virtual void YieldPoll();
  virtual int NicIfInit();

 public:
  explicit Runner(Device &dev_);

  /** Parse command line arguments. */
  int ParseArgs(int argc, char *argv[]);

  /** Run the simulation */
  int RunMain();

  /* these three are for `Runner::Device`. */
  void IssueDma(DMAOp &op);
  void MsiIssue(uint8_t vec);
  void MsiXIssue(uint8_t vec);
  void IntXIssue(bool level);
  void EthSend(const void *data, size_t len);

  void EventSchedule(TimedEvent &evt);
  void EventCancel(TimedEvent &evt);

  uint64_t TimePs() const;
  uint64_t GetMacAddr() const;
};

/**
 * Very simple device that just has one register size.
 */
template <class TReg = uint32_t>
class SimpleDevice : public Runner::Device {
 public:
  virtual TReg RegRead(uint8_t bar, uint64_t addr) = 0;
  virtual void RegWrite(uint8_t bar, uint64_t addr, TReg val) = 0;

  void RegRead(uint8_t bar, uint64_t addr, void *dest, size_t len) override {
    assert(len == sizeof(TReg));
    TReg r = RegRead(bar, addr);
    memcpy(dest, &r, sizeof(r));
  }

  void RegWrite(uint8_t bar, uint64_t addr, const void *src,
                size_t len) override {
    assert(len == sizeof(TReg));
    TReg r;
    memcpy(&r, src, sizeof(r));
    RegWrite(bar, addr, r);
  }
};

}  // namespace nicbm

#endif  // SIMBRICKS_NICBM_NICBM_H_
