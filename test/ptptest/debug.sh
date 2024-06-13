modprobe vfio-pci
dpdk-devbind.py -b vfio-pci 00:06.0
gdb --args ./build/ptpclient -l 0 -n 4 -b 00:02.0 -- -p 0x1 -T 0
