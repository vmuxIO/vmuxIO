# vMux

> Flexible and fast software-multiplexing of NICs for qemu VMs using vfio-user


Virtual Machines (VMs) are not only widely used in clouds to isolate tenant workloads, but increasingly also to deploy Virtual Network Functions (VNF).
From these use-cases emerges a need for flexible, scalable _and_ fast IO.
The current industry standard is however either not flexible nor scalable (passthrough + SR-IOV), or has high overheads (software switch + vhost).
To address these issues, we propose a novel architecture that can implement sophisticated multiplexing strategies in software without compromising in performance, dependability or security.
Our implementation, vMux, uses libvfio-user to implement device handling (emulation, mediation and passthrough) outside the hypervisor and Virtual Machine Monitor (VMM).
Our implementation is limited to qemu/KVM, but it is also applicable to all VMMs implementing the vfio-user interface (such as cloud-hypervisor).
Our vMux device multiplexer targets modern Network Interface Cards (NICs), in particular the Intel E810, but is generally applicable to most PCIe IO devices.

## State of development

We can:

- emluate registers
- do passthrough (e1000, E810)
- emulate an e1000

We cannot yet: 

- multiplex
- do interrupts
- emulate an E810/iavf

## Usage

Preconditions for passthrough:

- you have an intel E810 or e1000 NIC available in your system at say `0000:08:00.0`
- `0000:08:00.0` is bound to `vfio-pci`
- you are in a `nix shell github:vmuxio/vmuxio` to have the latest vmux available

Run vmux:

```bash
sudo vmux -d 0000:08:00.0 -s /tmp/vmux.sock -m passthrough
```

Run a VM with qemu using the vmux device:

```bash
qemu-system-x86_64
  -m 16G -object memory-backend-file,mem-path=/dev/shm/qemu-memory,prealloc=yes,id=bm,size=16G,share=on -numa node,memdev=bm \
  -device vfio-user-pci,socket=/tmp/vmux.sock \
  ...
```

## Develop

example for ryan.dse.in.tum.de:

load development shell with necessary packages

```shell
nix develop
```

build a few sofware packages and symlink the results

```shell
just build
```

run checks and bind correct dpdk/passthrough drivers

```shell
just prepare ./hosts/ryan.yaml 
ulimit -n 2048 # or set nixos systemd.extraConfig = ''DefaultLimitNOFILE=2048:524288''
```

do a performance measurement with moongen-lachnit and two or one PFs:

```shell
sudo ./mgln/bin/MoonGen ./mgln/bin/examples/l2-load-latency.lua 0 1 --rate 100000
sudo ./mgln/bin/MoonGen ./test/moonprogs/l2-load-latency.lua 0 00:00:00:00:00

```

setup vm images, start VMs and connect

```shell
# overwrite vm image with clean one
just vm-overwrite
# boot host-config and use qemu (instead of vmux) to pass through the E810 NIC
just vm
# ssh into it
just ssh
# load development environment
cd /mnt
nix develop
# load the driver
modprobe ice
# re-apply host-config to vm to apply nixos changes
just vm-update host-config
```

to use vMux instead of qemu for emulation/passthrough:

```shell
# start vMux
just vmuxPt
# or
just vmuxE810
# or
just vmuxE1000

# and then qemu
just vm-libvfio-user
```

## Demos

### vMux register emulation

Start vmux `just vmux` and qemu using `just vm-libvfio-user`. Connect to the VM `just ssh` and run:

```
# loading the ice driver still fails sadly: ice 0000:00:07.0: ice_init_hw failed: -105
dmesg | grep "ice 0000"
# but reading/writing registers works:
# read reset status (count)
devmem 0xfa0B8188
# write to register to trigger device reset
devmem 0xfa0b8190 32 1
# reset status has changed (bit 7:6 incremented)
devmem 0xfa0B8188
```

If you read too much `FFFFFFFF` or `DEADBEEF`, you probably need to reboot the machine/NIC.

### Passthrough

E810 passthrough: `just vm` to do qemu passthrough - `just vmux` and `just vm-libvfio-user` to do passthrough with vmux.

You can also pass through the simple e1000 NIC emulated by qemu with nested VMs:

```
just vm-libvfio-user-iommu
#In VM ("host")
just prepare-guest
just vmux-guest
just vm-libvfio-user-iommu-guest
# In nested VM ("guest")
just prepare-guest
./subprojects/vfio-e1000/e1000 0000:00:06.0
# on the native host running the "host" VM: observe how vfio-e1000 receives packets
ping 10.0.0.2
```

Use guest driver subprojects/vfio-e1000: `gcc -g -o e1000 e1000.c -DPOLL -DECHO`

Notes:

Passthrough only works, if you pass through all PFs to the same VM. If you want to do different VMs, you have to use VFs i guess.

Check if you receive any packets via sysfs `/sys/class/net/eth0/statistics/rx_packets`.


### Emulation

Currently uses kernel tap device as backend.

```bash
ip tuntap add mode tap tap-username0
ip addr add 10.2.0.1/24 dev tap-username0
ip link set dev tap-okelmann0 up
```

Now you can start vMux and the VM. In the VM, you'll have to bind an address as well:

```
ip addr add 10.2.0.2/24 dev enp0s6
```

Now, host and VM can communicate with each other over the emulated NIC.

Clean up once you are done:

```bash
ip link delete tap-username0
```


## Run benchmarks


To run passthrough tests on the DOS infrastructure, run autotest on `christina`. 
Ssh configs are set up there to work with `./test/conf/ssh_config_doctor_cluster`.
The buildscripts expect artefacts of `just build`, `just vm-overwrite` and `just docker-rebuild` on all involved physical hosts.

Write a config for the hosts you use: autotest_$DUT_$LOADGEN.cfg

```bash
nix develop
sln ./test/conf/autotest_rose_wilfred.cfg autotest.cfg 
```

Run scripted use-cases/benchmarks:

```bash
python3 ./test/src/measure_vnf.py -c ./test/conf/autotest_rose_wilfred.cfg -vvv
python3 ./test/src/measure_ycsb.py -c ./test/conf/autotest_rose_wilfred.cfg -vvv
python3 ./test/src/measure_hotel.py -c ./test/conf/autotest_rose_wilfred.cfg -vvv
ls /tmp/out1 # artefacts
```

Autotest micro-benchmarks:

```bash
# tests for big VMs:
python3 ./test/autotest -vvv -c test/conf/autotest_rose_wilfred.cfg run-guest -i vmux
python3 ./test/autotest -vvv -c test/conf/autotest_rose_wilfred.cfg test-load-lat-file -t test/conf/tests_multihost.cfg
# tests for small VMs:
python3 ./test/autotest -vvv -c test/conf/autotest_rose_wilfred_scalable.cfg run-guest -i vmux
python3 ./test/autotest -vvv -c test/conf/autotest_rose_wilfred_scalable.cfg test-load-lat-file -t test/conf/tests_scalable_multihost.cfg
ls ./outputs # artefacts
```

See also [autotest](test/README.md) for details.


# Debugging

## Coding style

`just autoformat`

- cpp code formatting as enforced by `clang-format`


## Notes on IOMMU/VFs

Iommu: check that it is enabled
```
$ find /sys | grep dmar
/sys/devices/virtual/iommu/dmar0
...
/sys/class/iommu/dmar0
/sys/class/iommu/dmar1
$ dmesg | grep IOMMU
... DMAR: IOMMU enabled
```

and find its groups at `/sys/kernel/iommu_groups`

check IOMMU state at `sudo cat /sys/kernel/debug/iommu/intel/dmar_translation_struct`

check IOMMU page table at `sudo cat /sys/kernel/debug/iommu/intel/domain_translation_struct` where `IOVA_PFN` is the IOVA (io virtual address; page aligned, so multiply with 0x1000) used by the device, and `PTE` is the corresponding host pysical address.

Use `kmod-tools-virt_to_phys_user` or `kmod-tools-pagemap_dump` from `nix/kmod-tools.nix` to check if your userspace actually maps the expected physical address.

Use qemus monitor (`Ctrl+A c` or exposable for automation via `-monitor tcp:127.0.0.1:2345,server,nowait`) to translate hva2gpa (with our patched qemu-libvfio.nix) or gpa2hva via monitor commands. You can also trigger the `e1000` to do a DMA operation with `dma_read e1000 0`.

## Pass through VFs

bind ice on pnic

sudo sh -c "echo 4 > /sys/class/net/enp24s0f0/device/sriov_numvfs"

bind vfio-pci on vf nics

boot vm with it


## Guest kernel modules:

```
nix develop .#host-kernel
tar -xvf $src
cd linux-*
cp $KERNELDIR/lib/modules/*/build/.config .config
make LOCALVERSION= scripts prepare modules_prepare
# build whatever module you want: (M=yourmodule)
make -C . M=drivers/block/null_blk
```

read more at (Hacking on Kernel Modules in NixOS)[https://blog.thalheim.io/2022/12/17/hacking-on-kernel-modules-in-nixos/]

## Debugging the kernel

```bash
just clone-linux
just build-linux
just vm-extkern
# attach gdb to linux kernel of VM
just gdb-vm-extkern
# in VM: run `mount -a` to mount hosts nix store
````
Using perf to find breakpointable/tracable lines:
```bash
sudo perf probe --vmlinux=./vmlinux --line schedule
# use -v with --add to show which variables we can trace at a location
sudo perf probe -v -n --vmlinux=./vmlinux --add "schedule:10 tsk"
# you can also use uprobe (-x) instead of kprobes (--vmlinux):
sudo perf probe -x ./build/vmux --add "_main:28 device" -n -v
````


## Qemu debug build

make sure you clone the correct qemu build
```
nix develop path/to/vmux#qemu-dev
```

use the `./configure` cmdline from `Makefile` and build with `make -j 12`

manually built, qemu needs to be started with an additional parameter: `-L /qemu-srcs/pc-bios`




