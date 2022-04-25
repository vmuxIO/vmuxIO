
default:
  @just --choose

# show help
help:
  just --list

# test two unused links with iperf2 (brittle and not idempotent): just hardware_loopback_test enp129s0f0 enp129s0f1 10.0.0.1 10.0.0.2 "-P 8"
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

ice_moongen:
  nix build .#moongen
  modprobe vfio-pci
  sudo ./result/libmoon/deps/dpdk/usertools/dpdk-devbind.py --bind=vfio-pci 81:00.0
  sudo ./result/libmoon/deps/dpdk/usertools/dpdk-devbind.py --bind=vfio-pci 81:00.1
  sudo ./result/bin/MoonGen ./result/bin/examples/l2-load-latency.lua 0 1
  echo this has no timestamping right now

build_dpdk:
  echo are you in nix develop nixos#dpdk?
  mkdir build
  meson build
  cd build
  meson configure -Dexamples=helloworld
  ninja # to build

dpdk_helloworld:
  sudo su -c "echo 8 > /sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages"
  mkdir /dev/huge1Gpages
  sudo mount -t hugetlbfs -o pagesize=1G nodev /dev/huge1Gpages
  sudo ./build/examples/dpdk-helloworld --lcores 2

  meson configure -Denable_kmods=true
  meson configure -Dkernel_dir=/nix/store/2g9vnkxppkx21jgkf08khkbaxpfxmj1s-linux-5.10.110-dev/lib/modules/5.10.110/build

pktgen: 
  nix shell .#pktgen
  sudo pktgen -l 0-4 --proc-type auto -- -P -m "[1:3].0, [2:4].1" -f ../Pktgen-DPDK/test/test_seq.lua
  # more cores doesnt help:
  sudo pktgen -l 0-17 --proc-type auto -- -P -m "[1-4:5-8].0, [9-12:13-16].1" -f ../Pktgen-DPDK/test/test_seq.lua
