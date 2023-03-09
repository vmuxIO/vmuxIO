# vmuxIO

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
# boot host-config
just vm
# ssh into it
just ssh
# re-apply host-config to vm
just vm-update host-config
```

## vMux demo

Start vmux `sudo ./build/vmux` and qemu using `just vm-libvfio-user`. Connect to the VM `just ssh` and run:

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
## Run benchmarks

See also [autotest](test/README.md) for details.

```bash
nix develop
just prepare ./hosts/christina_autotest.yaml
sln ./test/conf/autotest_okelmann_christina.cfg autotest.cfg
python3.10 ./test/autotest -vvv test-load-lat-file
ls output

```

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

pass through VFs:

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
