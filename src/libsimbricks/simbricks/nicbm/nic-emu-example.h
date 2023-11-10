/**
 * This is unused code. It acts as an example on how to use github.com/vmuxio/nic-emu as a library in a simbricks-like API
 */

#include "nic-emu.hpp"
#include "nicbm.h"

namespace nicbm {

static bool rust_logs_initialized = false;

class E1000Device : public Runner::Device {
    E1000FFI* e1000;

    static void send_cb(void *private_ptr, const uint8_t *buffer, uintptr_t len) {
        printf("Send CB\n");
    }

    static void dma_read_cb(void *private_ptr, uintptr_t dma_address, uint8_t *buffer, uintptr_t len) {
        printf("Dma read CB\n");
    }

    static void dma_write_cb(void *private_ptr, uintptr_t dma_address, const uint8_t *buffer, uintptr_t len) {
        printf("Dma write CB\n");
    }

    static void issue_interrupt_cb(void *private_ptr) {
        printf("Issue interrupt CB\n");
    }

    E1000Device() {
        if (!rust_logs_initialized) {
            initialize_rust_logging(4); // Debug logs
            rust_logs_initialized = true;
        }

        auto callbacks = FfiCallbacks {
            this,
            send_cb,
            dma_read_cb,
            dma_write_cb,
            issue_interrupt_cb,
        };

        e1000 = new_e1000(callbacks);
    }

    ~E1000Device() {
        drop_e1000(e1000);
    }

    void RegRead(uint8_t bar, uint64_t addr, void *dest, size_t len) override {
        e1000_region_access(e1000, bar, addr, (uint8_t*) dest , len, false);
    }

    void RegWrite(uint8_t bar, uint64_t addr, const void *src, size_t len) override {
        e1000_region_access(e1000, bar, addr, (uint8_t*) src, len, true);
    }

    void EthRx(uint8_t port, const void *data, size_t len) override {
        e1000_receive(e1000, (uint8_t*) data, len);
    }
};

}  // namespace nicbm
