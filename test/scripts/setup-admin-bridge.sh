#!/bin/bash

sudo ip link add br-gierens0 type bridge
sudo ip link set br-gierens0 up
sudo ip addr add 192.168.56.1/24 dev br-gierens0 
