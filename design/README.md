# vMux Design

## Event Loop / Runtime

- vMux main thread `main.cpp:main()`
    - polls Tap: reads from Tap and forwards it to guest
    - polls irq fds in case of passthrough to send interrupts: either during forwarding or afterwards (when for delayed interrupts)
- per-VM thread `VmuxRunner`
    - waits for the libvfio-user fd
    - exclusively reserved to server VM register accesses

