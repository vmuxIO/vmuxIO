import autotest as autotest
from configparser import ConfigParser
from argparse import (ArgumentParser, ArgumentDefaultsHelpFormatter, Namespace,
                      FileType, ArgumentTypeError)
from argcomplete import autocomplete
from logging import (info, debug, error, warning,
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
class IPerfTest:

    direction = "forward" # can be: forward | reverse | bidirectional

    # network options - currently set up to work with the "iperf_test.cfg" config
    guest_test_iface = "eth1"
    guest_hostname = "10.2.0.1"
    loadgen_hostname = "10.2.0.2"
    subnet_size = 24
    port = 5001



def main(measurement: Measurement, plan_only: bool = False) -> None:
    # general measure init
    host, loadgen = measurement.hosts()

    info("Booting VM")

    # boot VMs
    with measurement.virtual_machine(Interface.VFIO) as guest:
        # loadgen: set up interfaces and networking

        info('Binding loadgen interface')
        loadgen.modprobe_test_iface_drivers()
        loadgen.release_test_iface() # bind linux driver
        
        try:
            loadgen.delete_nic_ip_addresses(loadgen.test_iface)
        except Exception:
            pass

        loadgen.setup_test_iface_ip_net()
        guest.setup_test_iface_ip_net()

        ipt = IPerfTest()

        try:
            
            info("Starting iperf")
            guest.exec(f"sudo ip addr add {ipt.guest_hostname}/{ipt.subnet_size} dev {ipt.guest_test_iface}")
            loadgen.exec(f"sudo ip addr add {ipt.loadgen_hostname}/{ipt.subnet_size} dev {loadgen.test_iface}")

            guest.start_iperf_server(ipt)
            loadgen.run_iperf_client(ipt)

        finally:
            # teardown
            guest.stop_iperf_server()
            guest.exec(f"ip addr delete {ipt.guest_hostname}/{ipt.subnet_size} dev {ipt.guest_test_iface}")
            loadgen.exec(f"ip addr delete {ipt.loadgen_hostname}/{ipt.subnet_size} dev {loadgen.test_iface}")



if __name__ == "__main__":
    measurement = Measurement()
    main(measurement)
