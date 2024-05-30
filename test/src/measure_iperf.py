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
from dataclasses import dataclass, field, asdict
import subprocess
import os
from pandas import DataFrame



@dataclass
class IPerfTest(AbstractBenchTest):
    
    # test options
    direction: str # can be: forward | reverse | bidirectional
    output_json = True # sets / unsets -J flag on iperf client 

    interface: str # network interface used

    def test_infix(self):
        return f"iperf3_{self.num_vms}vms_{self.interface}_{self.direction}"

    def estimated_runtime(self) -> float:
        """
        estimate time needed to run this benchmark excluding boot time in seconds
        """
        return self.repetitions * (DURATION_S + 2)

    def find_error(self, repetition: int) -> bool:
        failure = False
        if not self.output_json:
            file = self.output_filepath(repetition)
            if os.stat(file).st_size == 0:
                error(f"Some iperf tests returned errors:\n{file}")
                failure = True
        return failure

    def summarize(self, repetition: int) -> DataFrame:
        with open(self.output_filepath(repetition), 'r') as f:
            data = json.load(f)

        # Extract important values
        start = data['start']
        end = data['end']
        intervals = data['intervals']

        duration_secs = end['sum_sent']['seconds']
        sent_bytes = end['sum_sent']['bytes']
        sent_bits_per_second = end['sum_sent']['bits_per_second']
        received_bytes = end['sum_received']['bytes']
        received_bits_per_second = end['sum_received']['bits_per_second']

        gbitps = received_bits_per_second / 1024 / 1024 / 1024

        data = [{
            **asdict(self), # put selfs member variables and values into this dict
            "repetition": repetition,
            # "vm_number": vm_number,
            "GBit/s": gbitps,
        }]
        return DataFrame(data=data)



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

    # set up test plan
    interfaces = [ 
          Interface.VFIO, 
          Interface.BRIDGE,
          Interface.BRIDGE_E1000,
          Interface.VMUX_PT, 
          Interface.VMUX_EMU, 
          # Interface.VMUX_EMU_E810, # tap backend not implemented for e810 (see #127)
          Interface.VMUX_DPDK, 
          Interface.VMUX_DPDK_E810 
          ]
    directions = [ "forward" ]
    vm_nums = [ 1 ] # 2 VMs are currently not supported for VMUX_DPDK*
    repetitions = 3
    DURATION_S = 61 if not BRIEF else 11
    if BRIEF:
        # interfaces = [ Interface.BRIDGE_E1000 ]
        interfaces = [ Interface.VMUX_DPDK_E810 ]
        directions = [ "forward" ]
        vm_nums = [ 1 ]
        repetitions = 1

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
    
    all_tests = []

    for num_vms in vm_nums:
        for interface in interfaces:

            # skip VM boots if possible
            test_matrix = dict(
                repetitions=[ repetitions ],
                interface=[ interface.value ],
                num_vms=[ num_vms ],
                direction=directions
                )
            if not IPerfTest.any_needed(test_matrix):
                warning(f"Skipping {interface}@{num_vms}VMs: All measurements done already.")
                continue

            for direction in directions:
                info(f"Testing configuration iface: {interface} direction: {direction} vm_num: {num_vms}")
                
                # build test object
                ipt = IPerfTest(
                        interface=interface.value,
                        repetitions=repetitions, 
                        direction=direction, 
                        num_vms=num_vms)
                all_tests += [ ipt ]

                info("Booting VM")

                # boot VMs
                with measurement.virtual_machine(interface) as guest:
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

                    for repetition in range(repetitions):
                        remote_output_file = "/tmp/iperf_result.json"
                        tmp_remote_output_file = "/tmp/tmp_iperf_result.json"
                        local_output_file = ipt.output_filepath(repetition)
                        loadgen.exec(f"sudo rm {remote_output_file} || true")
                        loadgen.exec(f"sudo rm {tmp_remote_output_file} || true")

                        info("Starting iperf")
                        guest.start_iperf_server(strip_subnet_mask(guest.test_iface_ip_net))
                        loadgen.run_iperf_client(ipt, DURATION_S, strip_subnet_mask(guest.test_iface_ip_net), remote_output_file, tmp_remote_output_file)
                        time.sleep(DURATION_S)
                        
                        try:
                            loadgen.wait_for_success(f'[[ -e {remote_output_file} ]]')
                        except TimeoutError:
                            error('Waiting for output file timed out.')
                        loadgen.copy_from(remote_output_file, local_output_file)

                        # teardown
                        guest.stop_iperf_server()
                        loadgen.stop_iperf_client()

                        # summarize results of VM
                        local_summary = ipt.output_filepath(repetition, extension = "summary")
                        with open(local_summary, 'w') as file:
                            try:
                                # to_string preserves all cols
                                summary = ipt.summarize(repetition).to_string()
                            except Exception as e:
                                summary = str(e)
                            file.write(summary) 


    for ipt in all_tests:
        for repetition in range(repetitions):
            ipt.find_error(repetition)


if __name__ == "__main__":
    measurement = Measurement()
    main(measurement)
