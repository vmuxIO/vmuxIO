# vMux Design

## Event Loop / Runtime

- vMux main thread `main.cpp:main()`
    - polls Tap: reads from Tap and forwards it to guest
    - polls irq fds in case of passthrough to send interrupts: either during forwarding or afterwards (for delayed interrupts)
- per-VM thread `VmuxRunner`
    - waits for the libvfio-user fd
    - exclusively reserved to server VM register accesses

## Packet copies/buffer

With DPDK backend: 1 excess copy, 0 excess queues

- E810EmulatedDevice calls driver to poll packets
    - `rte_eth_rx_burst()` 32 packets into buffers of `rte_mempool`
    - `class Dpdk` stores buffer pointers in itself
- E810EmulatedDevice copies buffers into guest DMA buffers/queues (`EthRx()`)
- E810EmulatedDevice tells Dpdk driver that packets have been consumed and buffers can be freed


## Multi-queue polling

(Only applies to DPDK driver)

- main thread: polls queues
- main thread: copies packets into DMA buffers
- runner threads: serves registers
