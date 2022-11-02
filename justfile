
default:
  @just --choose

# show help
help:
  just --list

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

build:
  nix build -o mg .#moongen
  nix build -o mg21 .#moongen21
  nix build -o mgln .#moongen-lachnit

dpdk-setup:
  modprobe vfio-pci
  sudo ./result/libmoon/deps/dpdk/usertools/dpdk-devbind.py --bind=vfio-pci 81:00.0
  sudo ./result/libmoon/deps/dpdk/usertools/dpdk-devbind.py --bind=vfio-pci 81:00.1
  sudo su -c "echo 8 > /sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages"
  mkdir /dev/huge1Gpages
  sudo mount -t hugetlbfs -o pagesize=1G nodev /dev/huge1Gpages
  sudo ./build/examples/dpdk-helloworld --lcores 2 # needed, because moongen cant load firmware

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
