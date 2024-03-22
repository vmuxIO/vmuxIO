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
    output_json = True # sets / unsets -J flag on iperf client 
    output_path = "/tmp/measure_iperf.json"


    # network options - currently set up to work with the "measure_christina_river.cfg" config
    guest_test_iface: str
    guest_hostname: str
    loadgen_hostname: str
    port = 5001


def strip_subnet_mask(ip_addr: str):
    return ip_addr[ : ip_addr.index("/")]


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


        guest.setup_test_iface_ip_net()
        loadgen.setup_test_iface_ip_net()

        ipt = IPerfTest(
            guest_hostname=strip_subnet_mask(guest.test_iface_ip_net), 
            loadgen_hostname=strip_subnet_mask(guest.test_iface_ip_net),
            guest_test_iface=guest.test_iface)

        try:
            
            info("Starting iperf")
            guest.start_iperf_server(ipt)
            loadgen.run_iperf_client(ipt)

        finally:
            # teardown
            guest.stop_iperf_server()
            loadgen.stop_iperf_client()



if __name__ == "__main__":
    measurement = Measurement()
    main(measurement)
