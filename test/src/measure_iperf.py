import autotest as autotest
from configparser import ConfigParser
from argparse import (ArgumentParser, ArgumentDefaultsHelpFormatter, Namespace,
                      FileType, ArgumentTypeError)
from argcomplete import autocomplete
from logging import (info, debug, error, warning, getLogger,
                     DEBUG, INFO, WARN, ERROR)
from server import Host, Guest, LoadGen
from enums import Machine, Interface, Reflector, MultiHost
from measure import Bench, AbstractBenchTest, Measurement, end_foreach
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
import traceback
from conf import G



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
        overheads = 35
        return (self.repetitions * (DURATION_S + 2) ) + overheads

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
    global DURATION_S

    # set up test plan
    interfaces = [
          Interface.VFIO,
          Interface.BRIDGE,
          Interface.BRIDGE_VHOST,
          Interface.BRIDGE_E1000,
          Interface.VMUX_PT,
          Interface.VMUX_EMU,
          # Interface.VMUX_EMU_E810, # tap backend not implemented for e810 (see #127)
          Interface.VMUX_DPDK,
          Interface.VMUX_DPDK_E810,
          Interface.VMUX_MED
          ]
    directions = [ "forward" ]
    vm_nums = [ 1 ] # 2 VMs are currently not supported for VMUX_DPDK*
    repetitions = 3
    DURATION_S = 61 if not G.BRIEF else 11
    if G.BRIEF:
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
    tests = IPerfTest.list_tests(test_matrix)

    if plan_only:
        return

    all_tests = []

    with Bench(
            tests = tests,
            args_reboot = ["interface", "num_vms", "direction"],
            brief = G.BRIEF
            ) as (bench, bench_tests):
        for num_vms, num_vms_tests in bench.iterator(bench_tests, "num_vms"):
            for interface, interface_tests in bench.iterator(num_vms_tests, "interface"):
                interface = Interface(interface)
                for direction, direction_tests in bench.iterator(interface_tests, "direction"):
                    assert len(direction_tests) == 1 # we have looped through all variables now, right?
                    test = direction_tests[0]

                    info(f"Running {test}")
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

                        guest.modprobe_test_iface_drivers(interface=interface)
                        guest.setup_test_iface_ip_net()
                        loadgen.setup_test_iface_ip_net()

                        for repetition in range(repetitions):
                            #cleanup
                            guest.stop_iperf_server()
                            loadgen.stop_iperf_client()

                            remote_output_file = "/tmp/iperf_result.json"
                            tmp_remote_output_file = "/tmp/tmp_iperf_result.json"
                            local_output_file = test.output_filepath(repetition)
                            loadgen.exec(f"sudo rm {remote_output_file} || true")
                            loadgen.exec(f"sudo rm {tmp_remote_output_file} || true")

                            info("Starting iperf")
                            guest.start_iperf_server(strip_subnet_mask(guest.test_iface_ip_net))
                            loadgen.run_iperf_client(test, DURATION_S, strip_subnet_mask(guest.test_iface_ip_net), remote_output_file, tmp_remote_output_file)
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
                            local_summary = test.output_filepath(repetition, extension = "summary")
                            with open(local_summary, 'w') as file:
                                try:
                                    # to_string preserves all cols
                                    summary = test.summarize(repetition).to_string()
                                except Exception as e:
                                    summary = traceback.format_exc()
                                file.write(summary)
                    # end VM
                    bench.done(test)

    for ipt in all_tests:
        for repetition in range(repetitions):
            ipt.find_error(repetition)


if __name__ == "__main__":
    measurement = Measurement()
    main(measurement)
