import autotest as autotest
from configparser import ConfigParser
from argparse import (ArgumentParser, ArgumentDefaultsHelpFormatter, Namespace,
                      FileType, ArgumentTypeError)
from argcomplete import autocomplete
from logging import (info, debug, error, warning, getLogger,
                     DEBUG, INFO, WARN, ERROR)
from server import Host, Guest, LoadGen, MultiHost
from enums import Machine, Interface, Reflector
from measure import AbstractBenchTest, Measurement, end_foreach, BRIEF, OUT_DIR
from util import safe_cast, product_dict
from typing import Iterator, cast, List, Dict, Callable, Tuple, Any
import time
from os.path import isfile, join as path_join
import yaml
import json
from root import *
from dataclasses import dataclass, field
import subprocess



@dataclass
class IPerfTest(AbstractBenchTest):
    
    # test options
    direction: str # can be: forward | reverse | bidirectional
    output_json = True # sets / unsets -J flag on iperf client 
    num_vms: int
    repetitions: int

    # network options
    guest_test_iface: str
    guest_hostname: str
    loadgen_hostname: str
    port = 5001

    def __init__(self, repetitions=1, direction="forward", interface=Interface.VFIO, num_vms=1):
        self.repetitions = repetitions
        self.direction = direction
        self.interface = interface
        self.num_vms = num_vms


    def test_infix(self):
        return f"iperf_{self.num_vms}vms_{self.interface}"

    def output_path_per_vm(self, repetition: int, vm_number: int) -> str:
        return str(Path(OUT_DIR) / "measure_iperf" / f"VMs_{self.test_infix()}_rep{repetition}" / f"vm{vm_number}.log")

    def estimated_runtime(self) -> float:
        """
        estimate time needed to run this benchmark excluding boot time in seconds
        """
        return 5 # TODO



def strip_subnet_mask(ip_addr: str):
    return ip_addr[ : ip_addr.index("/")]


def main(measurement: Measurement, plan_only: bool = False) -> None:
    host, loadgen = measurement.hosts()
    from measure import OUT_DIR as M_OUT_DIR, BRIEF as M_BRIEF
    global OUT_DIR
    global BRIEF
    global DURATION_S
    OUT_DIR = M_OUT_DIR
    BRIEF = M_BRIEF

    if BRIEF:
        info("Running single test with one VM")
        single_test()

    else:
        # set up test plan
        interfaces = [ Interface.VMUX_PT, Interface.VMUX_EMU_E810, Interface.VMUX_DPDK_E810 ]
        directions = [ "forward", "reverse", "bidirectional" ]
        vm_nums = [ 1, 2, 4 ]

        repetitions = 3

        test_matrix = dict(
            repetitions=[ repetitions ],
            direction=directions,
            interface=[ interface.value for interface in interfaces],
            num_vms=vm_nums
        )
        
        info(f"Iperf Test execution plan:")
        IPerfTest.estimate_time(test_matrix, ["interface", "num_vms"])

        if plan_only:
            return
        
        logger = getLogger()

        for iface in interfaces:
            for direction in directions:
                for vm_num in vm_nums:
                    info(f"Testing configuration iface: {iface} direction: {direction} vm_num: {vm_num}")
                    
                    # build test object^

                    test_config = IPerfTest(repetitions, direction, iface, vm_num)
                    # temporarily raise log level to avoid normal info logs to console for repeated tests
                    prevLevel = logger.level
                    logger.setLevel(WARN)

                    single_test(test_config, 0)

                    logger.setLevel(prevLevel)


def single_test(ipt=IPerfTest(), repetition=0):
    """
    Runs a single iperf test based on the supplied IPerfTest object. If no Object is supplied, use default config.
    """
    # general measure init
    host, loadgen = measurement.hosts()

    info("Booting VM")

    interface = Interface.VFIO

    # boot VMs
    with measurement.virtual_machine(ipt.interface) as guest:
        # loadgen: set up interfaces and networking

        info('Binding loadgen interface')
        loadgen.modprobe_test_iface_drivers()
        loadgen.release_test_iface() # bind linux driver
        
        try:
            loadgen.delete_nic_ip_addresses(loadgen.test_iface)
        except Exception:
            pass

        try:
            guest.setup_test_iface_ip_net()
            loadgen.setup_test_iface_ip_net()

            ipt.guest_hostname = strip_subnet_mask(guest.test_iface_ip_net) 
            ipt.loadgen_hostname = strip_subnet_mask(guest.test_iface_ip_net) 
            ipt.guest_test_iface = guest.test_iface

            info("Starting iperf")
            guest.start_iperf_server(ipt)
            loadgen.run_iperf_client(ipt, repetition)

        finally:
            # teardown
            guest.stop_iperf_server()

        

if __name__ == "__main__":
    measurement = Measurement()
    main(measurement)
