from enum import Enum

class Machine(Enum):
    # Machine types

    # host machine
    HOST = "host"

    # VM with machine type PC
    PCVM = "pcvm"

    # VM with machine type MicroVM
    MICROVM = "microvm"


class Interface(Enum):
    # Interface types

    # Physical NIC (only works with host machine type)
    PNIC = "pnic"

    # Bridge to physical NIC on host, and for VM additionally VirtIO NIC
    # connected to it via TAP device
    BRIDGE = "bridge"

    # Bridge to physical NIC on host, and for VM additionally VirtIO NIC
    # connected to it via TAP device. Uses linux kernel virtio vhost and 
    # overrides other vhost options to on.
    BRIDGE_VHOST = "bridge-vhost"

    # Same as BRIDGE, but with e1000 instead of virtio-net
    BRIDGE_E1000 = "bridge-e1000"

    # MacVTap to physical NIC on host, and for VM additionally VirtIO NIC
    # connected to it
    MACVTAP = "macvtap"

    # VFIO-passthrough to physical NIC for the VM
    VFIO = "vfio"

    # VMux-passthrough to physical NIC for the VM
    VMUX_PT = "vmux-pt"

    # vMux e1000 emulation to tap backend
    VMUX_EMU = "vmux-emu"

    # vMux e810 emulation to tap backend
    VMUX_EMU_E810 = "vmux-emu-e810"

    # vMux e810 mediation (uses dpdk)
    VMUX_MED = "vmux-med"

    # vMux e1000 emulation to dpdk backend
    VMUX_DPDK = "vmux-dpdk"

    # vMux e810 emulation to dpdk backend
    VMUX_DPDK_E810 = "vmux-dpdk-e810"


    def needs_br_tap(self) -> bool:
        return self in [ Interface.BRIDGE, Interface.BRIDGE_VHOST, Interface.BRIDGE_E1000, Interface.VMUX_EMU, Interface.VMUX_EMU_E810 ]

    def needs_macvtap(self) -> bool:
        return self in [ Interface.MACVTAP ]

    def needs_vfio(self) -> bool:
        return self in [ Interface.VFIO, Interface.VMUX_PT, Interface.VMUX_DPDK, Interface.VMUX_DPDK_E810, Interface.VMUX_MED ]

    def needs_vmux(self) -> bool:
        return self in [ Interface.VMUX_PT, Interface.VMUX_EMU, Interface.VMUX_DPDK, Interface.VMUX_EMU_E810, Interface.VMUX_MED, Interface.VMUX_DPDK_E810 ]

    def is_passthrough(self) -> bool:
        return self in [ Interface.VFIO, Interface.VMUX_PT ]

    def guest_driver(self) -> str:
        if self in [ Interface.VFIO, Interface.VMUX_PT, Interface.VMUX_EMU_E810, Interface.VMUX_MED, Interface.VMUX_DPDK_E810 ]:
            return "ice"
        if self in [ Interface.BRIDGE_E1000, Interface.VMUX_EMU, Interface.VMUX_DPDK ]:
            return "e1000"
        if self in [ Interface.BRIDGE, Interface.BRIDGE_VHOST, Interface.MACVTAP ]:
            return "virtio-net"
        raise Exception(f"Dont know which guest driver is used with {self}")

    @staticmethod
    def choices():
        # return a list of the values of all Enum options 
        return [ enum.value for enum in Interface.__members__.values() ]


class Reflector(Enum):
    # Reflector types

    # MoonGen reflector
    MOONGEN = "moongen"

    # XDP reflector
    XDP = "xdp"
