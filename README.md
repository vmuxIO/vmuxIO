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

Extremely incomplete. 

We can:

- emluate registers
- do passthrough (e1000, E810)

We cannot: 

- multiplex
- do interrupts
- ...

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

do a performance measurement with moongen-lachnit:

```shell
sudo ./mgln/bin/MoonGen ./mgln/bin/examples/l2-load-latency.lua 0 1 --rate 100000
```

setup vm images, start VMs and connect

```shell
# overwrite vm image with clean one
just vm-overwrite
# start vmux
just vmux
# boot host-config with vmux as NIC
just vm-libvfio-user
# ssh into it
just ssh
# load the driver
modprobe ice
# re-apply host-config to vm
just vm-update host-config
```

## vMux demo

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

## Run benchmarks

See also [autotest](test/README.md) for details.

```bash
nix develop
just prepare ./hosts/christina_autotest.yaml
sln ./test/conf/autotest_okelmann_christina.cfg autotest.cfg
python3.10 ./test/autotest -vvv test-load-lat-file
ls output

```
# Debugging

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

To use manually built linux kernels in nixos guest configs, have a look at [ktest](https://github.com/YellowOnion/ktest/blob/73fadcff949236927133141fcba4bfd76df632e7/kernel_install.nix)

## Qemu debug build

make sure you clone the correct qemu build
```
nix develop path/to/vmux#qemu-dev
```

use the `./configure` cmdline from `Makefile` and build with `make -j 12`

manually built, qemu needs to be started with an additional parameter: `-L /qemu-srcs/pc-bios`


## Debugging with qemus e1000

To do nested virtualisation with a simple, qemu emulated NIC:

```
just vm-libvfio-user-iommu
#In VM 
just prepare-guest
just vmux-guest
just vm-libvfio-user-iommu-guest
```

Use with mmisono/vfio-e1000: `gcc -g -o e1000 e1000.c -DPOLL -DECHO`


