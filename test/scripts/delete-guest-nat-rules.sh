#!/bin/bash

sudo iptables -t nat -D PREROUTING -p tcp -d 131.159.102.16 --dport 2222 -j DNAT --to-destination 192.168.56.20:22
sudo iptables -D FORWARD -o enp24s0f0 -i br-gierens0 -s 192.168.56.0/24 -m conntrack --ctstate NEW -j ACCEPT
sudo iptables -D FORWARD -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT
sudo iptables -t nat -D POSTROUTING -o enp24s0f0 -j MASQUERADE
