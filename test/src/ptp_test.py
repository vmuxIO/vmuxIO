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

    info("Running PTP test with one VM")
    single_test()


def single_test(repetition=0):
    # general measure init
    host, loadgen = measurement.hosts()

    info("Booting VM")

    # boot VMs
    with measurement.virtual_machine(Interface.VMUX_EMU_E810) as guest:
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

        guest_hostname = strip_subnet_mask(guest.test_iface_ip_net) 
        loadgen_hostname = strip_subnet_mask(guest.test_iface_ip_net) 

        breakpoint()


        # christina: sudo ptp4l -i enp81s0f0 -m -P -4
        # river: sudo ptp4l -i enp24s0f0 -m -P -4 -s
        

if __name__ == "__main__":
    measurement = Measurement()
    main(measurement)
