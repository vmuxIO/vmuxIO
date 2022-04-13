
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
