proot := justfile_directory()
host_extkern_image :=  proot + "/VMs/host-extkern-image.qcow2"
qemu_libvfiouser_bin := proot + "/qemu/bin/qemu-system-x86_64"
qemu_ssh_port := "2222"
user := `whoami`
vmuxSock := "/tmp/vmux-" + user + ".sock"
#vmuxSock := "/tmp/vmux.sock"

default:
  @just --choose

# show help
help:
  just --list
vmux DEVICE=`yq -r '.devices[] | select(.name=="ethDut") | ."pci"' hosts/$(hostname).yaml`:
  sudo {{proot}}/build/vmux -d {{DEVICE}} -s {{vmuxSock}}

# connect to `just qemu` vm
ssh COMMAND="":
  ssh \
  -i {{proot}}/nix/ssh_key \
  -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/dev/null \
  -o IdentityAgent=/dev/null \
  -F /dev/null \
  -p {{qemu_ssh_port}} \
  root@localhost -- "{{COMMAND}}"

# update nixos config running in vm (`just vm-update host` or host-extkern)
vm-update config:
  just ssh "cd /mnt && nixos-rebuild switch --flake .#{{config}}"

vm EXTRA_CMDLINE="" PASSTHROUGH=`yq -r '.devices[] | select(.name=="ethDut") | ."pci"' hosts/$(hostname).yaml`:
    sudo qemu-system-x86_64 \
        -cpu host \
        -enable-kvm \
        -m 8G \
        -device virtio-serial \
        -fsdev local,id=myid,path={{proot}},security_model=none \
        -device virtio-9p-pci,fsdev=myid,mount_tag=home,disable-modern=on,disable-legacy=off \
        -fsdev local,id=myNixStore,path=/nix/store,security_model=none \
        -device virtio-9p-pci,fsdev=myNixStore,mount_tag=myNixStore,disable-modern=on,disable-legacy=off \
        -drive file={{proot}}/VMs/host-image2.qcow2 \
        -net nic,netdev=user.0,model=virtio \
        -netdev user,id=user.0,hostfwd=tcp:127.0.0.1:{{qemu_ssh_port}}-:22 \
        -device vfio-pci,host={{PASSTHROUGH}} \
        -nographic

vm-libvfio-user:
    sudo qemu/bin/qemu-system-x86_64 \
        -cpu host \
        -enable-kvm \
        -m 8G -object memory-backend-file,mem-path=/dev/shm/qemu-memory,prealloc=yes,id=bm,size=8G,share=on -numa node,memdev=bm \
        -device virtio-serial \
        -fsdev local,id=myid,path={{proot}},security_model=none \
        -device virtio-9p-pci,fsdev=myid,mount_tag=home,disable-modern=on,disable-legacy=off \
        -fsdev local,id=myNixStore,path=/nix/store,security_model=none \
        -device virtio-9p-pci,fsdev=myNixStore,mount_tag=myNixStore,disable-modern=on,disable-legacy=off \
        -drive file={{proot}}/VMs/host-image.qcow2 \
        -net nic,netdev=user.0,model=virtio \
        -netdev user,id=user.0,hostfwd=tcp:127.0.0.1:{{qemu_ssh_port}}-:22 \
        -device vfio-user-pci,socket={{vmuxSock}} \
        -s \
        -nographic


@vm-libvfio-user-iommu:
    #!/usr/bin/env bash
    sudo ip tuntap add mode tap tap0
    sudo ip link set dev tap0 up
    sudo ip a add 10.0.0.1/24 dev tap0
    sudo {{qemu_libvfiouser_bin}}  \
        -L / \
        -cpu host \
        -netdev tap,id=net0,ifname=tap0,script=no,downscript=no \
        -device e1000,netdev=net0 \
        -machine q35,accel=kvm,kernel-irqchip=split \
        -device intel-iommu,intremap=on,device-iotlb=on \
        -m 16G -object memory-backend-file,mem-path=/dev/shm/qemu-memory,prealloc=yes,id=bm,size=16G,share=on -numa node,memdev=bm \
        -device virtio-serial \
        -fsdev local,id=myid,path={{proot}},security_model=none \
        -device virtio-9p-pci,fsdev=myid,mount_tag=home,disable-modern=on,disable-legacy=off \
        -fsdev local,id=myNixStore,path=/nix/store,security_model=none \
        -device virtio-9p-pci,fsdev=myNixStore,mount_tag=myNixStore,disable-modern=on,disable-legacy=off \
        -drive file={{proot}}/VMs/host-image.qcow2 \
        -net nic,netdev=user.0,model=virtio \
        -netdev user,id=user.0,hostfwd=tcp:127.0.0.1:{{qemu_ssh_port}}-:22 \
        -s \
        -nographic
      sudo ip link del tap0

prepare-guest:
    modprobe vfio-pci
    echo 8086 100e > /sys/bus/pci/drivers/vfio-pci/new_id
    #echo "vfio-pci" > /sys/bus/pci/devices/0000\:00\:03.0/driver_override 
    #echo 0000:00:03.0 > /sys/bus/pci/drivers/vfio-pci/bind 

vmux-guest:
    ./build/vmux -d 0000:00:03.0

vm-libvfio-user-iommu-guest:
    sudo {{qemu_libvfiouser_bin}}  \
        -L {{proot}}/qemu-manual/pc-bios/ \
        -cpu host \
        -machine q35,accel=kvm,kernel-irqchip=split \
        -device intel-iommu,intremap=on,device-iotlb=on,caching-mode=on \
        -m 1G -object memory-backend-file,mem-path=/dev/shm/qemu-memory,prealloc=yes,id=bm,size=1G,share=on -numa node,memdev=bm \
        -device virtio-serial \
        -fsdev local,id=myid,path=/mnt,security_model=none \
        -device virtio-9p-pci,fsdev=myid,mount_tag=home,disable-modern=on,disable-legacy=off \
        -fsdev local,id=myNixStore,path=/nix/store,security_model=none \
        -device virtio-9p-pci,fsdev=myNixStore,mount_tag=myNixStore,disable-modern=on,disable-legacy=off \
        -drive file=/mnt/VMs/host-image2.qcow2 \
        -net nic,netdev=user.0,model=virtio \
        -netdev user,id=user.0,hostfwd=tcp:127.0.0.1:{{qemu_ssh_port}}-:22 \
        -device vfio-user-pci,socket="/tmp/vmux.sock" \
        -s \
        -nographic

vm-noiommu-guest:
    sudo {{qemu_libvfiouser_bin}}  \
        -L {{proot}}/qemu-manual/pc-bios/ \
        -cpu host \
        -machine q35,accel=kvm,kernel-irqchip=split \
        -m 1G -object memory-backend-file,mem-path=/dev/shm/qemu-memory,prealloc=yes,id=bm,size=1G,share=on -numa node,memdev=bm \
        -device virtio-serial \
        -fsdev local,id=myid,path=/mnt,security_model=none \
        -device virtio-9p-pci,fsdev=myid,mount_tag=home,disable-modern=on,disable-legacy=off \
        -fsdev local,id=myNixStore,path=/nix/store,security_model=none \
        -device virtio-9p-pci,fsdev=myNixStore,mount_tag=myNixStore,disable-modern=on,disable-legacy=off \
        -drive file=/mnt/VMs/host-image2.qcow2 \
        -net nic,netdev=user.0,model=virtio \
        -netdev user,id=user.0,hostfwd=tcp:127.0.0.1:{{qemu_ssh_port}}-:22 \
        -s \
        -nographic

vm-libvfio-user-noiommu-guest:
    sudo {{qemu_libvfiouser_bin}}  \
        -L {{proot}}/qemu-manual/pc-bios/ \
        -cpu host \
        -machine q35,accel=kvm,kernel-irqchip=split \
        -m 1G -object memory-backend-file,mem-path=/dev/shm/qemu-memory,prealloc=yes,id=bm,size=1G,share=on -numa node,memdev=bm \
        -device virtio-serial \
        -fsdev local,id=myid,path=/mnt,security_model=none \
        -device virtio-9p-pci,fsdev=myid,mount_tag=home,disable-modern=on,disable-legacy=off \
        -fsdev local,id=myNixStore,path=/nix/store,security_model=none \
        -device virtio-9p-pci,fsdev=myNixStore,mount_tag=myNixStore,disable-modern=on,disable-legacy=off \
        -drive file=/mnt/VMs/host-image2.qcow2 \
        -net nic,netdev=user.0,model=virtio \
        -netdev user,id=user.0,hostfwd=tcp:127.0.0.1:{{qemu_ssh_port}}-:22 \
        -device vfio-user-pci,socket="/tmp/vmux.sock" \
        -s \
        -nographic

vm-libvfio-user-iommu-guest-passthrough:
    sudo {{qemu_libvfiouser_bin}}  \
        -L {{proot}}/qemu-manual/pc-bios/ \
        -cpu host \
        -machine q35,accel=kvm,kernel-irqchip=split \
        -device intel-iommu,intremap=on,device-iotlb=on,caching-mode=on \
        -m 4G -object memory-backend-file,mem-path=/dev/shm/qemu-memory,prealloc=yes,id=bm,size=4G,share=on -numa node,memdev=bm \
        -device virtio-serial \
        -fsdev local,id=myid,path=/mnt,security_model=none \
        -device virtio-9p-pci,fsdev=myid,mount_tag=home,disable-modern=on,disable-legacy=off \
        -fsdev local,id=myNixStore,path=/nix/store,security_model=none \
        -device virtio-9p-pci,fsdev=myNixStore,mount_tag=myNixStore,disable-modern=on,disable-legacy=off \
        -drive file=/mnt/VMs/host-image2.qcow2 \
        -net nic,netdev=user.0,model=virtio \
        -netdev user,id=user.0,hostfwd=tcp:127.0.0.1:{{qemu_ssh_port}}-:22 \
        -device vfio-pci,host="0000:00:03.0" \
        -s \
        -nographic

vm-libvfio-user-not-shared:
    sudo qemu-system-x86_64 \
        -cpu host \
        -enable-kvm \
        -m 8G \
        -device virtio-serial \
        -fsdev local,id=myid,path={{proot}},security_model=none \
        -device virtio-9p-pci,fsdev=myid,mount_tag=home,disable-modern=on,disable-legacy=off \
        -fsdev local,id=myNixStore,path=/nix/store,security_model=none \
        -device virtio-9p-pci,fsdev=myNixStore,mount_tag=myNixStore,disable-modern=on,disable-legacy=off \
        -drive file={{proot}}/VMs/host-image.qcow2 \
        -net nic,netdev=user.0,model=virtio \
        -netdev user,id=user.0,hostfwd=tcp:127.0.0.1:{{qemu_ssh_port}}-:22 \
        -device vfio-user-pci,socket={{vmuxSock}} \
        -s \
        -nographic

# not working
vm-extkern EXTRA_CMDLINE="":
    echo {{host_extkern_image}}
    sudo qemu-system-x86_64 \
        -cpu host \
        -enable-kvm \
        -m 500M \
        -device virtio-serial \
        -fsdev local,id=myid,path={{proot}},security_model=none \
        -device virtio-9p-pci,fsdev=myid,mount_tag=home,disable-modern=on,disable-legacy=off \
        -fsdev local,id=myNixStore,path=/nix/store,security_model=none \
        -device virtio-9p-pci,fsdev=myNixStore,mount_tag=myNixStore,disable-modern=on,disable-legacy=off \
        -hda {{host_extkern_image}} \
        -kernel /boot/EFI/nixos/3yzi7lf9lh56sx77zkjf3bwgd397zzxy-linux-5.15.77-bzImage.efi \
        -initrd /boot/EFI/nixos/widwkz9smm89f290c0vxs97wnkr0jwpn-initrd-linux-5.15.77-initrd.efi \
        -append "root=/dev/sda console=ttyS0 {{EXTRA_CMDLINE}}" \
        -net nic,netdev=user.0,model=virtio \
        -netdev user,id=user.0,hostfwd=tcp:127.0.0.1:{{qemu_ssh_port}}-:22 \
        -nographic
# -drive file={{host_extkern_image}} \
#-kernel {{proot}}/VMs/kernel/bzImage \
# -kernel {{APP}} -nographic
#-device virtio-net-pci,netdev=en0 \
#-netdev bridge,id=en0,br=virbr0 \

# test two unused links with iperf2 (brittle and not idempotent): just hardware_loopback_test enp129s0f0 enp129s0f1 10.0.0.1 10.0.0.2 "-P 8"
# Remeber to set the used devices as unmanaged in `networkctl list`.
hardware_loopback_test ETH1 ETH2 IP1 IP2 PERFARGS="" PREFIXSIZE="30":
  #!/bin/sh
  IPERF2=$(which iperf2)
  HANDLE=$(mktemp --tmpdir "loperf.XXXXXXXX")
  echo handle $HANDLE
  sudo unshare --net=$HANDLE echo new namespace created
  sudo ip link set dev {{ETH2}} netns $HANDLE
  sudo nsenter --net=$HANDLE ip addr add dev {{ETH2}} {{IP2}}/{{PREFIXSIZE}}
  sudo nsenter --net=$HANDLE ip link set dev {{ETH2}} up
  sudo ip addr add dev {{ETH1}} {{IP1}}/{{PREFIXSIZE}}

  echo Devices to be used in namespace:
  sudo nsenter --net=$HANDLE ip addr
  echo and on the host:
  ip addr show dev {{ETH1}}

  echo Start namespaced server in background.
  sudo nsenter --net=$HANDLE $IPERF2 -s &
  SERVERPID=$!
  echo pid $SERVERPID
  # wait for server to be started
  sleep 10
  echo "Start iperf2 client (test)."
  RESULT=$($IPERF2 -c {{IP2}} {{PERFARGS}})
  echo "$RESULT"

  sudo umount $HANDLE
  rm $HANDLE
  sudo ip addr del dev {{ETH1}} {{IP1}}/{{PREFIXSIZE}}
  sudo kill $(ps --ppid $SERVERPID -o pid=)
  echo -n "Waiting for server to be killed (pid $SERVERPID)..."
  wait
  echo done

prepare HOSTYAML:
  sudo nix develop -c ./hosts/prepare.py {{HOSTYAML}}

# prepare/configure this project for use
build:
  chmod 600 ./nix/ssh_key
  meson build --wipe
  meson compile -C build
  nix build -o {{proot}}/mg .#moongen
  nix build -o {{proot}}/mg21 .#moongen21
  nix build -o {{proot}}/mgln .#moongen-lachnit
  nix build -o {{proot}}/qemu .#qemu
  nix build -o {{proot}}/xdp .#xdp-reflector
  nix build -o {{proot}}/qemu-ioregionfd .#qemu-ioregionfd

vm-overwrite:
  mkdir -p {{proot}}/VMs
  nix build -o {{proot}}/VMs/kernel nixpkgs#linux
  # host-extkern VM
  nix build -o {{proot}}/VMs/host-extkern-image-ro .#host-extkern-image # read only
  install -D -m644 {{proot}}/VMs/host-extkern-image-ro/nixos.qcow2 {{host_extkern_image}}
  # host VM
  nix build -o {{proot}}/VMs/host-image-ro .#host-image # read only
  install -D -m644 {{proot}}/VMs/host-image-ro/nixos.qcow2 {{proot}}/VMs/host-image.qcow2
  install -D -m644 {{proot}}/VMs/host-image-ro/nixos.qcow2 {{proot}}/VMs/host-image2.qcow2
  # guest VM
  nix build -o {{proot}}/VMs/guest-image-ro .#guest-image # read only
  install -D -m644 {{proot}}/VMs/guest-image-ro/nixos.qcow2 {{proot}}/VMs/guest-image.qcow2
  qemu-img resize {{proot}}/VMs/guest-image.qcow2 +8G

dpdk-setup:
  modprobe vfio-pci
  sudo ./result/libmoon/deps/dpdk/usertools/dpdk-devbind.py --bind=vfio-pci 81:00.0
  sudo ./result/libmoon/deps/dpdk/usertools/dpdk-devbind.py --bind=vfio-pci 81:00.1
  sudo su -c "echo 8 > /sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages"
  mkdir /dev/huge1Gpages
  sudo mount -t hugetlbfs -o pagesize=1G nodev /dev/huge1Gpages
  sudo ./build/examples/dpdk-helloworld --lcores 2 # needed, because moongen cant load firmware

vmdq-example: 
  echo on christina with X550 with vfio-pci
  sudo ./examples/dpdk-vmdq_dcb -l 1-4 -n 4 -a 01:00.0 -a 01:00.1 -- -p 3 --nb-pools 32 --nb-tcs 4
  echo displays that it is forwarding stuff
  echo ice driver lacks vmdq impl

ice_moongen: dpdk-setup
  nix build .#moongen
  sudo ./result/bin/MoonGen ./result/bin/examples/l2-load-latency.lua 0 1
  echo this has no timestamping right now

dpdk21moongen:
  cd libmoon/deps/dpdk/build
  meson configure -Dtests=false -Denable_kmods=false -Dexamples=helloworld -Ddisable_drivers=kni -Ddefault_library=shared -Dmachine=nehalem -Dmax_lcores=256 -Dbuildtype=debug


build_dpdk:
  echo are you in nix develop .#dpdk? or .#moongen21?
  mkdir build
  meson build
  cd build
  meson configure -Dexamples=helloworld
  ninja # to build
  NIX_CFLAGS_COMPILE="$NIX_CFLAGS_COMPILE -O0" ninja -j128 # or this for debugging
  pushd libmoon/deps/dpdk/build; ninja; popd; pushd build; make; popd

dpdk_helloworld: dpdk-setup
  meson configure -Denable_kmods=true
  meson configure -Dkernel_dir=/nix/store/2g9vnkxppkx21jgkf08khkbaxpfxmj1s-linux-5.10.110-dev/lib/modules/5.10.110/build

pktgen: 
  nix shell .#pktgen
  sudo pktgen -l 0-4 --proc-type auto -- -P -m "[1:3].0, [2:4].1" -f ../Pktgen-DPDK/test/test_seq.lua
  # more cores doesnt help:
  sudo pktgen -l 0-17 --proc-type auto -- -P -m "[1-4:5-8].0, [9-12:13-16].1" -f ../Pktgen-DPDK/test/test_seq.lua

trex_bind:
  # t-rex-x64 compains that it wants igb_uio, but for ice we still need vfio-pci
  #nix-shell -p linuxPackages.dpdk-kmods
  #find /nix/store/74fzpcj8ww5pflnmc4m6y2q3j7w4kngm-dpdk-kmods-2021-04-21 | grep "igb_uio"
  #sudo insmod /nix/store/74fzpcj8ww5pflnmc4m6y2q3j7w4kngm-dpdk-kmods-2021-04-21/lib/modules/5.10.111/extra/igb_uio.ko.xz
  #sudo ./libmoon/deps/dpdk/usertools/dpdk-devbind.py --bind=igb_uio 81:00.0
  #sudo ./libmoon/deps/dpdk/usertools/dpdk-devbind.py --bind=igb_uio 81:00.1

  LD_LIBRARY_PATH=/nix/store/nfbxdafi7y4r780lvba4j0h30b8lhbx5-zlib-1.2.12/lib /nix/store/qjgj2642srlbr59wwdihnn66sw97ming-glibc-2.33-123/lib64/ld-linux-x86-64.so.2 ./_t-rex-64 --cfg ../simple_cfg.yaml --dump-interfaces
  LD_LIBRARY_PATH=/nix/store/nfbxdafi7y4r780lvba4j0h30b8lhbx5-zlib-1.2.12/lib /nix/store/qjgj2642srlbr59wwdihnn66sw97ming-glibc-2.33-123/lib64/ld-linux-x86-64.so.2 ./_t-rex-64 --cfg ../simple_cfg.yaml -f cap2/limit_multi_pkt.yaml -c 1 -m 1 -d 10

trex_ieee1588:
  # cd into modified v2.97
  cd automation/trex_control_plane/interactive/
  python3 udp_1pkt_src_ip_split_latency_ieee_1588.py

vfio-user-server:
  qemu-system-x86_64 \
  -machine x-remote,vfio-user=on \
  -netdev bridge,id=en0,br=virbr0 \
  -device virtio-net-pci,netdev=en0,id=ether1 \
  -nographic \
  -monitor unix:/home/mikilio/rem-sock,server,nowait \
  -object x-vfio-user-server,id=vfioobj1,type=unix,path=/tmp/remotesock,device=ether1

# use autotest tmux sessions: `just autotest-tmux ls`
autotest-tmux *ARGS:
  #!/usr/bin/env python3
  from configparser import ConfigParser, ExtendedInterpolation
  conf = ConfigParser(interpolation=ExtendedInterpolation())
  conf.read("{{proot}}/autotest.cfg")
  import os
  os.system(f"tmux -L {conf['common']['tmux_socket']} {{ARGS}}")

# connect to the autotest guest
autotest-ssh *ARGS:
  #!/usr/bin/env python3
  from configparser import ConfigParser, ExtendedInterpolation
  conf = ConfigParser(interpolation=ExtendedInterpolation())
  conf.read("{{proot}}/autotest.cfg")
  import os
  os.system(f"ssh -F {conf['host']['ssh_config']} {conf['guest']['fqdn']} {{ARGS}}")
  

# read list of hexvalues from stdin and find between which consecutive pairs arg1 lies
rangefinder *ARGS:
  #!/usr/bin/env python3
  import fileinput
  import argparse
  import shlex

  # parse args
  parser = argparse.ArgumentParser(
      prog='rangefinder',
      description='read list of hexvalues from stdin and find between which consecutive pairs arg1 lies'
      )
  parser.add_argument('key',
      help='key value to search a range for in which it falls (in hex)'
      )
  parser.add_argument('-c', '--column',
      default=0,
      type=int,
      help='choose a column from stdin (space separated, starts counting at 0)'
      )
  parser.add_argument('-i', '--inline',
      action='store_true',
      help='input ranges are specified inline as `fffa-fffb`'
      )
  args = parser.parse_args(shlex.split("{{ARGS}}"))

  # settings
  base = 16
  key = int(args.key, base=base)
  def sizeof_fmt(num, suffix="B"):
    for unit in ["", "Ki", "Mi", "Gi", "Ti", "Pi", "Ei", "Zi"]:
        if abs(num) < 1024.0:
            return f"{num:3.1f}{unit}{suffix}"
        num /= 1024.0
    return f"{num:.1f}Yi{suffix}"

  # state
  prev = 0
  current = 0

  # main:
  for line in fileinput.input():
    if args.inline:
      try:
        column = line.split(" ")[args.column]
        range = column.split("-") # throw meaningful error when this is not a 1-2 range
        prev = int(range[0], base=base)
        current = int(range[1], base=base)
      except Exception as e:
        print(e)
        continue
    else:
      try:
        column = line.split(" ")[args.column]
        # print(f"echo: {column}")
        integer = int(column, base=base)
      except Exception as e:
        print(e)
        continue
      prev = current
      current = integer

    # print(f"{prev}-{current}")
    if prev <= key and key < current:
      size = sizeof_fmt(current-prev)
      print(f"{prev:x} <= {key:x} <= {current:x} - size {size}")

read PID ADDR:
  #!/usr/bin/env python
  a = open("/proc/{{PID}}/mem", 'rb', 0)
  a.seek(0x{{ADDR}})
  r = int.from_bytes(a.read(8), byteorder="little")
  print(f"0x{r:016x}")

write PID ADDR VALUE:
  #!/usr/bin/env python
  a = open("/proc/{{PID}}/mem", 'wb', 0)
  a.seek(0x{{ADDR}})
  a.write(int.to_bytes(8, 0x{{VALUE}}, byteorder="little"))
