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
# read reset status (count)
devmem 0xfa0B8188
# write to register to trigger device reset
devmem 0xfa0b8190 32 1
# reset status has changed (bit 7:6 incremented)
devmem 0xfa0B8188
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
