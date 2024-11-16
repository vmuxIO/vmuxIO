proot := justfile_directory()
host_extkern_image :=  proot + "/VMs/nesting-host-extkern-image.qcow2"
qemu_libvfiouser_bin := proot + "/qemu/bin/qemu-system-x86_64"
qemu_ssh_port := "2222"
user := `whoami`
vmuxSock := "/tmp/vmux-" + user + ".sock"
qemuMem := "/dev/shm/qemu-memory-" + user
#vmuxSock := "/tmp/vmux.sock"
vmuxTap := "tap-" + user + "0"
linux_dir := "/scratch/" + user + "/vmux-linux"
linux_repo := "https://github.com/vmuxio/linux"
linux_rev := "nixos-linux-5.15.89"
nix_results := proot + "/kernel_shell"
kernel_shell := "$(nix build --out-link " + nix_results + "/kernel-fhs --json " + justfile_directory() + "#kernel-deps | jq -r '.[] | .outputs | .out')/bin/linux-kernel-build"

default:
  @just --choose

# show help
help:
  just --list

autoformat:
  clang-format -i src/* devices/* || true

uml:
  nix shell github:bkryza/clang-uml --command clang-uml-wrapped -g mermaid -c ./docs/clang-uml.yml
  echo You may view the result .mmd files with https://marmaid.live

# vmux passthrough (uses config: hosts/yourhostname.yaml)
vmux DEVICE=`yq -r '.devices[] | select(.name=="ethDut") | ."pci_full"' hosts/$(hostname).yaml`:
  sudo {{proot}}/build/vmux -d {{DEVICE}} -s {{vmuxSock}}

vmuxE810:
  sudo ip link delete {{vmuxTap}} || true
  sudo ip tuntap add mode tap {{vmuxTap}}
  sudo ip addr add 10.2.0.1/24 dev {{vmuxTap}}
  sudo ip link set dev {{vmuxTap}} up
  sudo gdb --args {{proot}}/build/vmux -d none -t {{vmuxTap}} -m emulation -s {{vmuxSock}} -b 52:54:00:fa:00:60 -q
  sudo ip link delete {{vmuxTap}}

vmuxE1000:
  sudo ip link delete {{vmuxTap}} || true
  sudo ip tuntap add mode tap {{vmuxTap}}
  sudo ip addr add 10.2.0.1/24 dev {{vmuxTap}}
  sudo ip link set dev {{vmuxTap}} up
  sudo {{proot}}/build/vmux -d none -t {{vmuxTap}} -m e1000-emu -s {{vmuxSock}} -q -b 52:54:00:fa:00:60
  sudo ip link delete {{vmuxTap}}

vmuxE1000b BRIDGE="br-okelmann" IF="enp65s0np0":
  sudo ip link set {{IF}} down || true
  sudo ip link delete {{vmuxTap}} || true
  sudo ip link delete {{BRIDGE}} || true

  sudo ip link add {{BRIDGE}} type bridge
  sudo ip addr add 10.2.0.1/24 dev {{BRIDGE}}
  sudo ip link set {{IF}} master {{BRIDGE}}

  sudo ip tuntap add mode tap {{vmuxTap}}
  sudo ip link set {{vmuxTap}} master {{BRIDGE}}

  sudo ip link set dev {{IF}} up
  sudo ip link set dev {{BRIDGE}} up
  sudo ip link set dev {{vmuxTap}} up
  sudo {{proot}}/build_release/vmux -d none -t {{vmuxTap}} -m e1000-emu -s {{vmuxSock}} -q -b 52:54:00:fa:00:60

  sudo ip link set {{IF}} down || true
  sudo ip link delete {{vmuxTap}} || true
  sudo ip link delete {{BRIDGE}} || true

vmuxDpdkE1000:
  sudo {{proot}}/build_release/vmux -u -q -d none -m e1000-emu -s {{vmuxSock}} -- -l 1 -n 1

vmuxDpdkE810:
  sudo {{proot}}/build/vmux -u -q -d none -m emulation -s {{vmuxSock}} -- -l 1 -n 1 -a 0000:81:00.0 -v

vmuxMed:
  sudo gdb --args {{proot}}/build/vmux -u -q -d none -m mediation -s {{vmuxSock}} -- -l 1 -n 1 -a 0000:81:00.0

vmuxVdpdk:
  sudo gdb --args {{proot}}/build/vmux -u -q -d none -m vdpdk -s {{vmuxSock}} -- -l 1 -n 1 -a 0000:81:00.0

vmuxMedLog:
  sudo {{proot}}/build/vmux -u -d none -m mediation -s {{vmuxSock}} -- -l 1 -n 1 -a 0000:81:00.0 2>&1 | rg 'ice_callback|send|qemu|dominik'

vmuxMedPerf:
  #!/usr/bin/env bash

  # from perf-record man page
  ctl_fifo=/tmp/perf_{{user}}_ctl.fifo
  test -p ${ctl_fifo} && unlink ${ctl_fifo}
  mkfifo ${ctl_fifo}
  exec {ctl_fd}<>${ctl_fifo}

  ctl_ack_fifo=/tmp/perf_{{user}}_ctl_ack.fifo
  test -p ${ctl_ack_fifo} && unlink ${ctl_ack_fifo}
  mkfifo ${ctl_ack_fifo}
  exec {ctl_fd_ack}<>${ctl_ack_fifo}
  
  sudo perf record -D -1 --control "fifo:${ctl_fifo},${ctl_ack_fifo}" -F 400 -g -- {{proot}}/build/vmux -u -q -d none -m mediation -s {{vmuxSock}} -- -l 1 -n 1 -a 0000:81:00.0 &
  perf_pid=$!

  sleep 5 && read -p 'Press enter to start recording.'
  echo "Enabling perf"
  echo 'enable' >&${ctl_fd} && read -u ${ctl_fd_ack} e1 && echo "enabled(${e1})"
  sleep 60
  echo "Disabling perf"
  echo 'disable' >&${ctl_fd} && read -u ${ctl_fd_ack} d1 && echo "disabled(${d1})"

  exec {ctl_fd_ack}>&-
  unlink ${ctl_ack_fifo}

  exec {ctl_fd}>&-
  unlink ${ctl_fifo}

  wait -n ${perf_pid}

  sudo chown dominik perf.data
  perf script >vmuxMed.perf

vmuxDpdkE810Gdb:
  sudo gdb --args {{proot}}/build/vmux -u -q -d none -m emulation -s {{vmuxSock}} -- -l 1 -n 1 -a 0000:c4:00.0

nic-emu:
  sudo ip link delete {{vmuxTap}} || true
  sudo ip tuntap add mode tap {{vmuxTap}}
  sudo ip addr add 10.2.0.1/24 dev {{vmuxTap}}
  sudo ip link set dev {{vmuxTap}} up
  sudo ~/nic-emu/target/release/nic-emu-cli -s /tmp/vmux-okelmann.sock -t tap-okelmann0

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
        -smp 4 \
        -enable-kvm \
        -m 16G \
        -machine q35,accel=kvm,kernel-irqchip=split \
        -device intel-iommu,intremap=on,device-iotlb=on,caching-mode=on \
        -device virtio-serial \
        -fsdev local,id=myid,path={{proot}},security_model=none \
        -device virtio-9p-pci,fsdev=myid,mount_tag=home,disable-modern=on,disable-legacy=off \
        -fsdev local,id=myNixStore,path=/nix/store,security_model=none \
        -device virtio-9p-pci,fsdev=myNixStore,mount_tag=myNixStore,disable-modern=on,disable-legacy=off \
        -drive file={{proot}}/VMs/nesting-host-image.qcow2 \
        -net nic,netdev=user.0,model=virtio \
        -netdev user,id=user.0,hostfwd=tcp:127.0.0.1:{{qemu_ssh_port}}-:22 \
        -device vfio-pci,host={{PASSTHROUGH}} \
        -nographic


# uses host kernel and initrd for host-extkern
prepare-direct-boot HOST="guest":
    mkdir -p {{proot}}/nix/results
    nix build .#nixosConfigurations.{{HOST}}.config.boot.kernelPackages.kernel --out-link {{proot}}/nix/results/{{HOST}}-kernel
    nix build .#nixosConfigurations.{{HOST}}.config.system.build.initialRamdisk --out-link {{proot}}/nix/results/{{HOST}}-initrd
    rm {{proot}}/nix/results/{{HOST}}-kernelParams || true
    nix eval --write-to {{proot}}/nix/results/{{HOST}}-kernelParams .#nixosConfigurations.{{HOST}}.config.boot.kernelParams --apply 'builtins.concatStringsSep " "'


vm-libvfio-user:
    sudo rm {{qemuMem}} || true
    sudo qemu/bin/qemu-system-x86_64 \
        -cpu host \
        -enable-kvm \
        -m 16G -object memory-backend-file,mem-path={{qemuMem}},prealloc=yes,id=bm,size=16G,share=on -numa node,memdev=bm \
        -machine q35,accel=kvm,kernel-irqchip=split \
        -device intel-iommu,intremap=on,device-iotlb=on,caching-mode=on \
        -device virtio-serial \
        -fsdev local,id=myid,path={{proot}},security_model=none \
        -device virtio-9p-pci,fsdev=myid,mount_tag=home,disable-modern=on,disable-legacy=off \
        -fsdev local,id=myNixStore,path=/nix/store,security_model=none \
        -device virtio-9p-pci,fsdev=myNixStore,mount_tag=myNixStore,disable-modern=on,disable-legacy=off \
        -drive file={{proot}}/VMs/nesting-host-image.qcow2 \
        -net nic,netdev=user.0,model=virtio \
        -netdev user,id=user.0,hostfwd=tcp:127.0.0.1:{{qemu_ssh_port}}-:22 \
        -device vfio-user-pci,socket={{vmuxSock}} \
        -s \
        -nographic

vm-e1000:
    sudo ip link delete {{vmuxTap}} || true
    sudo ip tuntap add mode tap {{vmuxTap}}
    sudo ip addr add 10.2.0.1/24 dev {{vmuxTap}}
    sudo ip link set dev {{vmuxTap}} up
    sudo rm {{qemuMem}} || true
    sudo qemu/bin/qemu-system-x86_64 \
        -cpu host \
        -smp 8 \
        -enable-kvm \
        -m 16G -object memory-backend-file,mem-path={{qemuMem}},prealloc=yes,id=bm,size=16G,share=on -numa node,memdev=bm \
        -machine q35,accel=kvm,kernel-irqchip=split \
        -device intel-iommu,intremap=on,device-iotlb=on,caching-mode=on \
        -device virtio-serial \
        -fsdev local,id=myid,path={{proot}},security_model=none \
        -device virtio-9p-pci,fsdev=myid,mount_tag=home,disable-modern=on,disable-legacy=off \
        -fsdev local,id=myNixStore,path=/nix/store,security_model=none \
        -device virtio-9p-pci,fsdev=myNixStore,mount_tag=myNixStore,disable-modern=on,disable-legacy=off \
        -drive file={{proot}}/VMs/nesting-host-image.qcow2 \
        -net nic,netdev=user.0,model=virtio \
        -netdev user,id=user.0,hostfwd=tcp:127.0.0.1:{{qemu_ssh_port}}-:22 \
        -netdev tap,id=admin1,ifname={{vmuxTap}},script=no,downscript=no \
        -device e1000,netdev=admin1,mac=02:34:56:78:9a:bc \
        -s \
        -nographic
# ,queues=4

# Launch a VM to test libvfio-user in a VM
# We pass-through an e1000 device (0000:00:03.0) with vmux (or vfio)
@vm-libvfio-user-iommu:
    #!/usr/bin/env bash
    sudo ip tuntap add mode tap tap0
    sudo ip link set dev tap0 up
    sudo ip a add 10.0.0.1/24 dev tap0
    sudo rm {{qemuMem}} || true
    sudo {{qemu_libvfiouser_bin}}  \
        -L / \
        -cpu host \
        -netdev tap,id=net0,ifname=tap0,script=no,downscript=no \
        -device e1000,netdev=net0 \
        -machine q35,accel=kvm,kernel-irqchip=split \
        -device intel-iommu,intremap=on,device-iotlb=on \
        -m 16G -object memory-backend-file,mem-path={{qemuMem}},prealloc=yes,id=bm,size=16G,share=on -numa node,memdev=bm \
        -device virtio-serial \
        -fsdev local,id=myid,path={{proot}},security_model=none \
        -device virtio-9p-pci,fsdev=myid,mount_tag=home,disable-modern=on,disable-legacy=off \
        -fsdev local,id=myNixStore,path=/nix/store,security_model=none \
        -device virtio-9p-pci,fsdev=myNixStore,mount_tag=myNixStore,disable-modern=on,disable-legacy=off \
        -drive file={{proot}}/VMs/nesting-host-image.qcow2 \
        -net nic,netdev=user.0,model=virtio \
        -netdev user,id=user.0,hostfwd=tcp:127.0.0.1:{{qemu_ssh_port}}-:22 \
        -s \
        -nographic
      sudo ip link del tap0

vm-strace-nonet EXTRA_CMDLINE="":
    sudo strace -o /tmp/strace-nonet qemu-system-x86_64 \
        -cpu host \
        -smp 4 \
        -enable-kvm \
        -m 16G \
        -machine q35,accel=kvm,kernel-irqchip=split \
        -device intel-iommu,intremap=on,device-iotlb=on,caching-mode=on \
        -device virtio-serial \
        -fsdev local,id=myid,path={{proot}},security_model=none \
        -device virtio-9p-pci,fsdev=myid,mount_tag=home,disable-modern=on,disable-legacy=off \
        -fsdev local,id=myNixStore,path=/nix/store,security_model=none \
        -device virtio-9p-pci,fsdev=myNixStore,mount_tag=myNixStore,disable-modern=on,disable-legacy=off \
        -drive file={{proot}}/VMs/nesting-host-image.qcow2 \
        -nographic

vm-strace-vmux:
    sudo rm {{qemuMem}} || true
    sudo strace -o /tmp/strace-vmux qemu/bin/qemu-system-x86_64 \
        -cpu host \
        -enable-kvm \
        -m 16G -object memory-backend-file,mem-path={{qemuMem}},prealloc=yes,id=bm,size=16G,share=on -numa node,memdev=bm \
        -machine q35,accel=kvm,kernel-irqchip=split \
        -device intel-iommu,intremap=on,device-iotlb=on,caching-mode=on \
        -device virtio-serial \
        -fsdev local,id=myid,path={{proot}},security_model=none \
        -device virtio-9p-pci,fsdev=myid,mount_tag=home,disable-modern=on,disable-legacy=off \
        -fsdev local,id=myNixStore,path=/nix/store,security_model=none \
        -device virtio-9p-pci,fsdev=myNixStore,mount_tag=myNixStore,disable-modern=on,disable-legacy=off \
        -drive file={{proot}}/VMs/nesting-host-image.qcow2 \
        -device vfio-user-pci,socket={{vmuxSock}} \
        -nographic

vm-strace-allnet:
    sudo ip link delete {{vmuxTap}} || true
    sudo ip tuntap add mode tap {{vmuxTap}}
    sudo ip addr add 10.2.0.1/24 dev {{vmuxTap}}
    sudo ip link set dev {{vmuxTap}} up
    sudo rm {{qemuMem}} || true
    sudo strace -o /tmp/strace-allnet qemu/bin/qemu-system-x86_64 \
        -cpu host \
        -smp 8 \
        -enable-kvm \
        -m 16G -object memory-backend-file,mem-path={{qemuMem}},prealloc=yes,id=bm,size=16G,share=on -numa node,memdev=bm \
        -machine q35,accel=kvm,kernel-irqchip=split \
        -device intel-iommu,intremap=on,device-iotlb=on,caching-mode=on \
        -device virtio-serial \
        -fsdev local,id=myid,path={{proot}},security_model=none \
        -device virtio-9p-pci,fsdev=myid,mount_tag=home,disable-modern=on,disable-legacy=off \
        -fsdev local,id=myNixStore,path=/nix/store,security_model=none \
        -device virtio-9p-pci,fsdev=myNixStore,mount_tag=myNixStore,disable-modern=on,disable-legacy=off \
        -drive file={{proot}}/VMs/nesting-host-image.qcow2 \
        -net nic,netdev=user.0,model=virtio \
        -netdev user,id=user.0,hostfwd=tcp:127.0.0.1:{{qemu_ssh_port}}-:22 \
        -netdev tap,id=admin1,ifname={{vmuxTap}},script=no,downscript=no \
        -device e1000,netdev=admin1,mac=02:34:56:78:9a:bc \
        -nographic

prepare-guest:
    modprobe vfio-pci
    echo 8086 100e > /sys/bus/pci/drivers/vfio-pci/new_id
    #echo "vfio-pci" > /sys/bus/pci/devices/0000\:00\:03.0/driver_override 
    #echo 0000:00:03.0 > /sys/bus/pci/drivers/vfio-pci/bind 

# start vmux in a VM
vmux-guest DEVICE="0000:00:03.0":
    ./build/vmux -d {{DEVICE}}

# start nested guest w/ viommu, w/ vmux (lib-vfio) device
vm-libvfio-user-iommu-guest:
    sudo rm {{qemuMem}} || true
    sudo {{qemu_libvfiouser_bin}}  \
        -L {{proot}}/qemu-manual/pc-bios/ \
        -cpu host \
        -machine q35,accel=kvm,kernel-irqchip=split \
        -device intel-iommu,intremap=on,device-iotlb=on,caching-mode=on \
        -m 1G -object memory-backend-file,mem-path={{qemuMem}},prealloc=yes,id=bm,size=1G,share=on -numa node,memdev=bm \
        -device virtio-serial \
        -fsdev local,id=myid,path=/mnt,security_model=none \
        -device virtio-9p-pci,fsdev=myid,mount_tag=home,disable-modern=on,disable-legacy=off \
        -fsdev local,id=myNixStore,path=/nix/store,security_model=none \
        -device virtio-9p-pci,fsdev=myNixStore,mount_tag=myNixStore,disable-modern=on,disable-legacy=off \
        -drive file=/mnt/VMs/nesting-guest-image.qcow2 \
        -net nic,netdev=user.0,model=virtio \
        -netdev user,id=user.0,hostfwd=tcp:127.0.0.1:{{qemu_ssh_port}}-:22 \
        -device vfio-user-pci,socket="/tmp/vmux.sock" \
        -s \
        -nographic

# start nested guest w/o viommu, w/o vmux (libvfio) device
vm-noiommu-guest:
    sudo rm {{qemuMem}} || true
    sudo {{qemu_libvfiouser_bin}}  \
        -L {{proot}}/qemu-manual/pc-bios/ \
        -cpu host \
        -machine q35,accel=kvm,kernel-irqchip=split \
        -m 1G -object memory-backend-file,mem-path={{qemuMem}},prealloc=yes,id=bm,size=1G,share=on -numa node,memdev=bm \
        -device virtio-serial \
        -fsdev local,id=myid,path=/mnt,security_model=none \
        -device virtio-9p-pci,fsdev=myid,mount_tag=home,disable-modern=on,disable-legacy=off \
        -fsdev local,id=myNixStore,path=/nix/store,security_model=none \
        -device virtio-9p-pci,fsdev=myNixStore,mount_tag=myNixStore,disable-modern=on,disable-legacy=off \
        -drive file=/mnt/VMs/nesting-guest-image-noiommu.qcow2 \
        -net nic,netdev=user.0,model=virtio \
        -netdev user,id=user.0,hostfwd=tcp:127.0.0.1:{{qemu_ssh_port}}-:22 \
        -s \
        -nographic

# start nested guest w/o viommu, w/ vmux (libvfio) device
vm-libvfio-user-noiommu-guest:
    sudo rm {{qemuMem}} || true
    sudo {{qemu_libvfiouser_bin}}  \
        -L {{proot}}/qemu-manual/pc-bios/ \
        -cpu host \
        -machine q35,accel=kvm,kernel-irqchip=split \
        -m 1G -object memory-backend-file,mem-path={{qemuMem}},prealloc=yes,id=bm,size=1G,share=on -numa node,memdev=bm \
        -device virtio-serial \
        -fsdev local,id=myid,path=/mnt,security_model=none \
        -device virtio-9p-pci,fsdev=myid,mount_tag=home,disable-modern=on,disable-legacy=off \
        -fsdev local,id=myNixStore,path=/nix/store,security_model=none \
        -device virtio-9p-pci,fsdev=myNixStore,mount_tag=myNixStore,disable-modern=on,disable-legacy=off \
        -drive file=/mnt/VMs/nesting-guest-image-noiommu.qcow2 \
        -net nic,netdev=user.0,model=virtio \
        -netdev user,id=user.0,hostfwd=tcp:127.0.0.1:{{qemu_ssh_port}}-:22 \
        -device vfio-user-pci,socket="/tmp/vmux.sock" \
        -s \
        -nographic

# use qemu with gdb command: handle all nostop pass
# sudo gdb --args {{qemu_libvfiouser_bin}}  \
#        -device vfio-pci,host=0000:00:03.0 \
#        -device vfio-user-pci,socket="/tmp/vmux.sock" \

# start nested guest w/ viommu, w/ vfio (not vmux) device
vm-libvfio-user-iommu-guest-passthrough:
    sudo rm {{qemuMem}} || true
    sudo {{qemu_libvfiouser_bin}}  \
        -L {{proot}}/qemu-manual/pc-bios/ \
        -cpu host \
        -machine q35,accel=kvm,kernel-irqchip=split \
        -device intel-iommu,intremap=on,device-iotlb=on,caching-mode=on \
        -m 4G -object memory-backend-file,mem-path={{qemuMem}},prealloc=yes,id=bm,size=4G,share=on -numa node,memdev=bm \
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
    qemu-system-x86_64 -s \
        -cpu host \
        -enable-kvm \
        -m 500M \
        -machine q35,accel=kvm,kernel-irqchip=split \
        -device intel-iommu,intremap=on,device-iotlb=on,caching-mode=on \
        -device virtio-serial \
        -fsdev local,id=myid,path={{proot}},security_model=none \
        -device virtio-9p-pci,fsdev=myid,mount_tag=home,disable-modern=on,disable-legacy=off \
        -fsdev local,id=myNixStore,path=/nix/store,security_model=none \
        -device virtio-9p-pci,fsdev=myNixStore,mount_tag=myNixStore,disable-modern=on,disable-legacy=off \
        -kernel {{linux_dir}}/arch/x86/boot/bzImage \
        -hda {{host_extkern_image}} \
        -append "root=/dev/sda console=ttyS0 nokaslr intel_iommu=on iommu=pt vfio_iommu_type1.allow_unsafe_interrupts=1 vfio_iommu_type1.dma_entry_limit=4294967295 {{EXTRA_CMDLINE}}" \
        -net nic,netdev=user.0,model=virtio \
        -netdev user,id=user.0,hostfwd=tcp:127.0.0.1:{{qemu_ssh_port}}-:22 \
        -nographic
# -drive file={{host_extkern_image}} \
#-kernel {{proot}}/VMs/kernel/bzImage \
# -kernel {{APP}} -nographic
#-device virtio-net-pci,netdev=en0 \
#-netdev bridge,id=en0,br=virbr0 \

# attach gdb to linux in vm-extkern
gdb-vm-extkern:
  gdb \
  -ex "add-auto-load-safe-path {{linux_dir}}" \
  -ex "target remote :1234" \
  -ex "file {{linux_dir}}/vmlinux"


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

prepare HOSTYAML=`echo ./hosts/$(hostname).yaml`:
  sudo nix develop -c ./hosts/prepare.py {{HOSTYAML}}

# prepare/configure this project for use
build:
  chmod 600 ./nix/ssh_key
  pushd subprojects/nic-emu; cargo build --no-default-features --features generate-bindings; cargo build --no-default-features --features generate-bindings --release; popd
  if [[ -d build ]]; then meson build --wipe; else meson build; fi
  pushd subprojects/nic-emu; cargo build --no-default-features --features generate-bindings; cargo build --no-default-features --features generate-bindings --release; popd
  meson compile -C build
  meson setup build_release --wipe -Dbuildtype=release
  meson compile -C build_release
  clang++ {{proot}}/test/kni-latency/kni-latency.cpp -o {{proot}}/test/kni-latency/kni-latency -O3
  nix build -o {{proot}}/mg .#moongen
  nix build -o {{proot}}/mg21 .#moongen21
  nix build -o {{proot}}/mgln .#moongen-lachnit
  nix build -o {{proot}}/qemu .#qemu
  nix build -o {{proot}}/xdp .#xdp-reflector
  nix build -o {{proot}}/qemu-ioregionfd .#qemu-ioregionfd
  nix build -o {{proot}}/ycsb .#ycsb
  nix build -o {{proot}}/fastclick .#fastclick
  nix build -o {{proot}}/vmux-nixbuild .#vmux
  pushd ./test/ptptest; make; popd
  [[ -z $(git submodule status | grep "^-") ]] || echo WARN: git submodules status: not in sync

update:
  # update nix flake inputs
  nix flake update
  # use `nix flake lock --update-input INPUT` to update a selected input only

  # update git submodules to the latest version of their branch
  git submodule update --remote --recursive
  # use `git submodule update --remote -- PATH` to update a selected git submodule only

docker-rebuild:
  cd subprojects/deathstarbench/wrk2; docker build -t wrk2d .

  yq -r ".services[] | .image" subprojects/deathstarbench/hotelReservation/docker-compose.yml | xargs -I{} sh -c "docker pull {} || echo Some repositories are expected not to exist because we will build them ourselves in the next step"
  cd subprojects/deathstarbench/hotelReservation; docker-compose build
  docker image save -o {{proot}}/VMs/docker-images-hotelReservation.tar $(yq -r ".services[] | .image" subprojects/deathstarbench/hotelReservation/docker-compose.yml)
  # cd subprojects/deathstarbench/hotelReservation; docker-compose up
  # cd subprojects/deathstarbench/hotelReservation; docker run -ti --mount type=bind,source=$(pwd)/wrk2,target=/wrk2 --network host wrk2 wrk -D exp -t 1 -c 1 -d 1 -L -s ./wrk2/scripts/hotel-reservation/mixed-workload_type_1.lua http://localhost:5000 -R 1

  yq -r ".services[] | .image" subprojects/deathstarbench/socialNetwork/docker-compose.yml | xargs -I{} sh -c "docker pull {} || echo Some repositories are expected not to exist because we will build them ourselves in the next step"
  cd subprojects/deathstarbench/socialNetwork; docker-compose build
  docker image save -o {{proot}}/VMs/docker-images-socialNetwork.tar $(yq -r ".services[] | .image" subprojects/deathstarbench/socialNetwork/docker-compose.yml)

  yq -r ".services[] | .image" subprojects/deathstarbench/mediaMicroservices/docker-compose.yml | xargs -I{} sh -c "docker pull {} || echo Some repositories are expected not to exist because we will build them ourselves in the next step"
  cd subprojects/deathstarbench/mediaMicroservices; docker-compose build
  docker image save -o {{proot}}/VMs/docker-images-mediaMicroservices.tar $(yq -r ".services[] | .image" subprojects/deathstarbench/mediaMicroservices/docker-compose.yml)


vm-init:
  #!/usr/bin/env python3
  import subprocess
  import os
  # Create directory for VMs cloud-init
  os.makedirs(f"{{proot}}/VMs/cloud-init", exist_ok=True)

  # IP address calculation
  import ipaddress
  start_ip = ipaddress.IPv4Address("192.168.56.20")


  for i in range(1, 801):
    print(f"wrinting cloud-init {i}")
    ip = f"{start_ip + i - 1}"

    # Network configuration
    network_config = f"""
  version: 2
  ethernets:
    admin0:
      addresses:
        - {ip}/255.255.248.0
      gateway4: 192.168.1.254
    """
    with open(f"/tmp/network-data-{{user}}.yml", "w") as file:
      file.write(network_config)
      
    # User data configuration
    user_data = f"""
  #cloud-config
  preserve_hostname: false
  hostname: guest{i}
    """
    with open(f"/tmp/user-data-{{user}}.yml", "w") as file:
      file.write(user_data)

    # Create cloud-init disk image
    subprocess.run(["cloud-localds", f"--network-config=/tmp/network-data-{{user}}.yml", 
                    f"{{proot}}/VMs/cloud-init/vm{i}.img", f"/tmp/user-data-{{user}}.yml"])

  import shutil
  shutil.copy("{{proot}}/VMs/cloud-init/vm1.img", "{{proot}}/VMs/cloud-init/vm0.img")


vm-overwrite NUM="35": vm-init
  #!/usr/bin/env bash
  set -x
  set -e
  mkdir -p {{proot}}/VMs
  just prepare-direct-boot test-guest
  just prepare-direct-boot host

  # build images fast

  overwrite() {
    install -D -m644 {{proot}}/VMs/ro-$1/nixos.qcow2 {{proot}}/VMs/$1.qcow2
    qemu-img resize {{proot}}/VMs/$1.qcow2 +8g
  }

  # when evaluating lots of images: --eval-max-memory-size 20000 --eval-workers 8 -j 16 --skip-cached
  nix-fast-build -f .#all-images --out-link {{proot}}/VMs/ro
  overwrite nesting-host-image
  overwrite nesting-host-extkern-image
  overwrite nesting-guest-image
  overwrite nesting-guest-image-noiommu
  overwrite test-guest-image
  for i in $(seq 1 {{NUM}}); do
    # roughly, but not quite overwrite test-guest-extkern-image$i
    install -D -m644 {{proot}}/VMs/ro-test-guest-image/nixos.qcow2 {{proot}}/VMs/test-guest-image$i.qcow2
    qemu-img resize {{proot}}/VMs/test-guest-image$i.qcow2 +16g
  done

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

fastclick-gen:
  ./fastclick/bin/click --dpdk -l 0 -a 0000:00:06.0 --log-level "pmd.net.vdpdk*:debug" -- ./test/fastclick/pktgen-l2.click

fastclick-reflect:
  ./fastclick/bin/click --dpdk -l 0 -a 0000:00:06.0 --log-level "pmd.net.vdpdk*:debug" -- ./test/fastclick/dpdk-bounce.click

fastclick-tap:
  ./fastclick/bin/click --dpdk -l 0 -a 0000:00:06.0 --log-level "pmd.net.vdpdk*:debug" -- ./test/fastclick/dpdk-tap.click

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

dpvs:
  just prepare
  echo 8192 | sudo tee /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages
  echo 8192 | sudo tee /sys/devices/system/node/node1/hugepages/hugepages-2048kB/nr_hugepages
  sudo mount -t hugetlbfs -o pagesize=2M nodev /tmp/mnt
  echo spawn exactly one VF and bind it to vfio
  echo make sure dpdk-devbind only detects exactly one compatible device (otherwise dpvs fails)
  sudo modprobe rte_kni carrier=on
  echo with E810 VF (iavf) the following inits and starts polling
  echo with E810 PF (ice) the following inits and start polling, but seems to use some fallback function (NETIF: dpdk_set_mc_list: rte_eth_dev_set_mc_addr_list is not supported, enable all multicast.)
  sudo ./result/bin/dpvs -c ./nix/dpvs.conf.single-nic.sample -- -l 0-8

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
  import importlib.util
  spec = importlib.util.spec_from_file_location("default_parser", "test/src/conf.py")
  default_parser = importlib.util.module_from_spec(spec)
  spec.loader.exec_module(default_parser)
  conf = default_parser.default_config_parser()
  conf.read("{{proot}}/autotest.cfg")
  import os
  os.system(f"tmux -L {conf['common']['tmux_socket']} {{ARGS}}")

# connect to the autotest guest
autotest-ssh *ARGS:
  #!/usr/bin/env python3
  from configparser import ConfigParser, ExtendedInterpolation
  import importlib.util
  spec = importlib.util.spec_from_file_location("default_parser", "test/src/conf.py")
  default_parser = importlib.util.module_from_spec(spec)
  spec.loader.exec_module(default_parser)
  conf = default_parser.default_config_parser()
  conf.read("{{proot}}/autotest.cfg")
  import os
  sudo = ""
  if conf["host"]["ssh_as_root"]:
    sudo = "sudo "
  cmd = f"{sudo}ssh -F {conf['host']['ssh_config']} {conf['guest']['fqdn']} {{ARGS}}"
  print(f"$ {cmd}")
  os.system(cmd)

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

# Git clone linux kernel
clone-linux:
  #!/usr/bin/env bash
  set -euo pipefail
  if [[ ! -d {{linux_dir}} ]]; then
    git clone {{linux_repo}} {{linux_dir}}
  fi

  set -x
  commit="{{linux_rev}}"
  if [[ $(git -C {{linux_dir}} rev-parse HEAD) != "$commit" ]]; then
     git -C {{linux_dir}} fetch {{linux_repo}} $commit
     git -C {{linux_dir}} checkout "$commit"
     rm -f {{linux_dir}}/.config
  fi


# Configure linux kernel build
configure-linux: #clone-linux
  #!/usr/bin/env bash
  set -xeuo pipefail
  if [[ ! -f {{linux_dir}}/.config ]]; then
    cd {{linux_dir}}
    {{kernel_shell}} "make defconfig kvm_guest.config"
    {{kernel_shell}} "scripts/config \
       --disable DRM \
       --disable USB \
       --disable WIRELESS \
       --disable WLAN \
       --disable SOUND \
       --disable SND \
       --disable HID \
       --disable INPUT \
       --disable NFS_FS \
       --disable ETHERNET \
       --disable NETFILTER \
       --enable DEBUG_INFO \
       --enable GDB_SCRIPTS \
       --enable DEBUG_DRIVER \
       --enable KVM \
       --enable KVM_INTEL \
       --enable KVM_AMD \
       --enable KVM_IOREGION \
       --enable BPF_SYSCALL \
       --enable CONFIG_MODVERSIONS \
       --enable IKHEADERS \
       --enable IKCONFIG_PROC \
       --enable VIRTIO_MMIO \
       --enable VIRTIO_MMIO_CMDLINE_DEVICES \
       --enable PTDUMP_CORE \
       --enable PTDUMP_DEBUGFS \
       --enable OVERLAY_FS \
       --enable SQUASHFS \
       --enable SQUASHFS_XZ \
       --enable SQUASHFS_FILE_DIRECT \
       --enable PVH \
       --disable SQUASHFS_FILE_CACHE \
       --enable SQUASHFS_DECOMP_MULTI \
       --disable SQUASHFS_DECOMP_SINGLE \
       --disable SQUASHFS_DECOMP_MULTI_PERCPU \
       --enable VFIO \
       --enable VFIO_IOMMU_TYPE1 \
       --enable VFIO_VIRQFD \
       --enable VFIO_NOIOMMU \
       --enable VFIO_PCI_CORE \
       --enable VFIO_PCI_MMAP \
       --enable VFIO_PCI_INTX \
       --enable VFIO_PCI \
       --enable VFIO_PCI_VGA \
       --enable VFIO_PCI_IGD \
       --enable VFIO_MDEV \
       --enable IOMMU_DEBUGFS \
       --enable INTEL_IOMMU_DEBUGFS \
       "
  fi

# Build linux kernel
build-linux: configure-linux
  #!/usr/bin/env bash
  set -xeu
  cd {{linux_dir}}
  #{{kernel_shell}} "make -C {{linux_dir}} oldconfig"
  yes "" | {{kernel_shell}} "make -C {{linux_dir}} -j$(nproc)"

# Linux kernel development shell
build-linux-shell:
  {{kernel_shell}} bash

irqs *ARGS:
  python3 {{proot}}/subprojects/irq-rates.py {{ARGS}}

gen-ssh-config:
  #!/usr/bin/env python3
  import ipaddress
  start = ipaddress.IPv4Address("192.168.56.20")
  for i in range(0, 801):
    print(f"Host vm{i + 1}.*")
    print(f"Hostname {start + i}")
    print("")

gen-admin-macs MAC:
  #!/usr/bin/env python3
  import importlib.util
  spec = importlib.util.spec_from_file_location("default_parser", "test/src/enums.py")
  enums = importlib.util.module_from_spec(spec)
  import json
  spec.loader.exec_module(enums)
  macs = ' '.join([ enums.MultiHost.mac('{{MAC}}', i) for i in range(0, 801) ])
  data = {
    'comment': 'Generated with `just gen-admin-macs`',
    'macs': macs
  }
  filename = "{{proot}}/nix/admin-macs.json"
  with open(filename, "w") as f:
    json.dump(data , f)
  print(f"Wrote macs to {filename}")




