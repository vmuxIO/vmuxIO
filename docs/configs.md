# Test config documentation



Variables are injected by autotest. Use e.g. via \$\{username\}.
projectDirectory = \$\{absolute path to the root directory of this git project\}
projectDirectoryName = \$\{name of the root directory\}
projectDirectoryParent = \$\{parent directory in which the project root resides in\}
username = \$\{current username\}

### [common] section
home_dir: the users home directory 
tmux_socket: the tmux socket name for vmux

### [local] section
moonprogs_dir: The path to the directory containining the moongen scripts

### [host] section
**fqdn**
 hostname of the host machine (e.g. river.dse.in.tum.de)

**admin_bridge**
 Name of the admin bridge interface to give the guest internet access and allow SSH login to it.

**admin_bridge_ip_ne**
 IP address and netmask of the admin bridge interface.


**admin_tap**
Name of the TAP interface of the guest which is connected to the admin
bridge, to give the guest internet access and allow SSH login to it.

**test_iface**
Name of the physical interface to test, so the one that either serves
as DuT directly or is used by the TAP/MacVTAP interface of the guest that
acts as DuT.

**test_iface_addr**
PCI address of the physical test interface.


**test_iface_mac**
MAC address of the physical test interface.


**test_iface_driv**
Default driver of the physical test interface.


**test_iface_dpdk_driv**
DPDK driver of the physical test interface.


**test_iface_vfio_driv**
DPDK driver for vfio and vmux passthrough of the physical test interface to the guest


**test_bridge**
Name of the test bridge for Linux bridge/TAP interfaces.

**test_bridge_ip_net**
IP address and netmask assigned to the bridge. Only needed for iperf


**test_tap**
Name of the test TAP interface for Linux bridge/TAP interfaces.


**test_macvtap**
Name of the test MacVTAP interface.


**vmux_socket_path**
Path to the vmux socket

**tmux_socket**
Name of the tmux socket.


**moongen_dir**
Path to the MoonGen installation.


**moonprogs_dir**
Path to the MoonGen scripts.


**xdp_reflector_dir**
Path to the XDP reflector installation.


**qemu_path**
Path to the QEMU installation.



**vmux_qemu_path**
Path to the QEMU installation for the vmux (libvfio-user) fork


**root_disk_file**
Path to the guest root disk image.


**fsdevs**
Name path pairs for the fs devices to mount into the guest.
Note: the name is also used as mount tag in the guest.


**ssh_config**
Optionally specify an ssh config file to use instead of the default one (see `man 5 ssh_config`).

**ssh_as_root**
Optionally run ssh clients as root

### [guest] section 

**fqdn**
Hostname of the guest.

**vcpus**
Number of vCPUs to assign to the guest.


**memory**
Amount of memory to assign to the guest in MiB.


**admin_iface_mac**
MAC address of the guest's admin interface.


**test_iface**
Name of the guest's test interface.


**test_iface_addr**
 PCI bus address of the guest's test interface.


**test_iface_mac**
MAC address of the guest's test interface.


**test_iface_ip_net**
IP address and netmask assigned to the test interface. Only needed for iperf


**test_iface_driv**
Default driver of the guest's test interface.


**test_iface_dpdk_driv**
DPDK driver of the guest's test interface.

**tmux_socket**
Name of the tmux socket.


**moongen_dir**
Path to the MoonGen installation on the guest.

**moonprogs_dir**
Path to the MoonGen scripts on the guest.

**xdp_reflector_dir**
Path to the XDP reflector installation on the guest.

**ssh_config**
Optionally specify an ssh config file to use instead of the default one (see `man 5 ssh_config`).


### [loadgen] section 
**fqdn**
 hostname of the host machine (e.g. river.dse.in.tum.de)

**test_iface**
Name of the physical interface to test, so the one that either serves
as DuT directly or is used by the TAP/MacVTAP interface of the guest that
acts as DuT.

**test_iface_addr**
PCI address of the physical test interface.


**test_iface_mac**
MAC address of the physical test interface.


**test_iface_driv**
Default driver of the physical test interface.


**tmux_socket**
Name of the tmux socket.

**moongen_dir**
Path to the MoonGen installation.

**moonprogs_dir**
Path to the MoonGen scripts.


**ssh_config**
Optionally specify an ssh config file to use instead of the default one (see `man 5 ssh_config`).
