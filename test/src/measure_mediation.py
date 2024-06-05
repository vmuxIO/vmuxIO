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
from pandas import DataFrame, read_csv



@dataclass
class MediationTest(AbstractBenchTest):
    
    # test options
    interface: str # network interface used

    def test_infix(self):
        return f"mediation_{self.num_vms}vms_{self.interface}"

    def output_filepath_histogram(self, repetition: int) -> str:
        return self.output_filepath(repetition, extension = "histogram.csv")

    def output_filepath_throughput(self, repetition: int) -> str:
        return self.output_filepath(repetition, extension = "throughput.csv")

    def output_filepath_fastclick(self, repetition: int) -> str:
        return self.output_filepath(repetition, extension = "fastclick.log")

    def estimated_runtime(self) -> float:
        """
        estimate time needed to run this benchmark excluding boot time in seconds
        """
        return self.repetitions * (DURATION_S + 2)

    # def find_error(self, repetition: int) -> bool:
    #     failure = False
    #     if not self.output_json:
    #         file = self.output_filepath(repetition)
    #         if os.stat(file).st_size == 0:
    #             error(f"Some iperf tests returned errors:\n{file}")
    #             failure = True
    #     return failure

    def summarize(self, repetition: int) -> DataFrame:
        with open(self.output_filepath_fastclick(repetition), 'r') as f:
            fastclickLog: List[str] = f.readlines()
        with open(self.output_filepath_throughput(repetition), 'r') as f:
            moongen = read_csv(f)

        # parse fastclickLog
        packet_count_lines = [line for line in fastclickLog if "thread " in line and " count:" in line]
        packet_counts = [int(line.strip().split(' ')[-1]) for line in packet_count_lines]
        rx_packets = sum(packet_counts)

        # parse moongen
        tx_packets = moongen[moongen.Direction=='TX'].tail(1)['TotalPackets'].iloc[0]
        tx_mpps = moongen[moongen.Direction=='TX'].tail(1)['PacketRate'].iloc[0]

        packet_loss = (tx_packets - rx_packets) / rx_packets
        rx_mpps_calc = (rx_packets / tx_packets) * tx_mpps

        data = [{
            **asdict(self), # put selfs member variables and values into this dict
            "repetition": repetition,
            "rxPackets": rx_packets,
            "txPackets": tx_packets,
            "txMpps": tx_mpps,
            "rxMppsCalc": rx_mpps_calc,
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
          Interface.VMUX_DPDK_E810 
          ]
    vm_nums = [ 1 ] # 2 VMs are currently not supported for VMUX_DPDK*
    repetitions = 3
    DURATION_S = 61 if not BRIEF else 11
    if BRIEF:
        # interfaces = [ Interface.BRIDGE_E1000 ]
        interfaces = [ Interface.VMUX_DPDK_E810 ]
        vm_nums = [ 1 ]
        repetitions = 1

    test_matrix = dict(
        repetitions=[ repetitions ],
        interface=[ interface.value for interface in interfaces],
        num_vms=vm_nums
    )
    
    info(f"Iperf Test execution plan:")
    MediationTest.estimate_time(test_matrix, ["interface", "num_vms"])

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
                )
            if not MediationTest.any_needed(test_matrix):
                warning(f"Skipping {interface}@{num_vms}VMs: All measurements done already.")
                continue

            info(f"Testing configuration iface: {interface} vm_num: {num_vms}")
            
            # build test object
            ipt = MediationTest(
                    interface=interface.value,
                    repetitions=repetitions, 
                    num_vms=num_vms)
            all_tests += [ ipt ]

            info("Booting VM")

            # boot VMs
            with measurement.virtual_machine(interface) as guest:
                # loadgen: set up interfaces and networking

                info('Binding loadgen interface')
                loadgen.modprobe_test_iface_drivers()
                loadgen.bind_test_iface() # bind vfio driver

                # guest.modprobe_test_iface_drivers()
                guest.bind_test_iface() # bind vfio driver

                for repetition in range(repetitions):
                    remote_fastclick_log = "/tmp/fastclick.log"
                    local_fastclick_log = ipt.output_filepath_fastclick(repetition)
                    remote_moongen_throughput = "/tmp/throughput.csv"
                    local_moongen_throughput = ipt.output_filepath_throughput(repetition)
                    remote_moongen_histogram = "/tmp/histogram.csv"
                    local_moongen_histogram = ipt.output_filepath_histogram(repetition)
                    local_summary_file = ipt.output_filepath(repetition)
                    guest.exec(f"sudo rm {remote_fastclick_log} || true")
                    loadgen.exec(f"sudo rm {remote_moongen_throughput} || true")
                    loadgen.exec(f"sudo rm {remote_moongen_histogram} || true")

                    info("Starting Fastclick")
                    guest.start_fastclick("test/fastclick/mac-switch-software.click", remote_fastclick_log, script_args={'ifacePCI0': guest.test_iface_addr})
                    info("Starting MoonGen")
                    loadgen.run_l2_load_latency(loadgen, "00:00:00:00:00:00", 1000, 
                            runtime = DURATION_S,
                            size = 60,
                            nr_macs = 3,
                            histfile = remote_moongen_histogram,
                            statsfile = remote_moongen_throughput,
                            outfile = '/tmp/output.log'
                            )
                    time.sleep(DURATION_S + 5)

                    # await moongen done
                    try:
                        loadgen.wait_for_success(f'[[ -e {remote_moongen_histogram} ]]')
                    except TimeoutError:
                        error('Waiting for moongen output file timed out.')

                    # await fastclick logs
                    guest.stop_fastclick()
                    try:
                        guest.wait_for_success(f'[[ $(tail -n 5 {remote_fastclick_log}) = *"AUTOTEST_DONE"* ]]')
                    except TimeoutError:
                        error('Waiting for fastclick output file to appear timed out')

                    # teardown
                    loadgen.stop_l2_load_latency(loadgen)
                    guest.kill_fastclick()

                    # collect artefacts
                    guest.copy_from(remote_fastclick_log, local_fastclick_log)
                    loadgen.copy_from(remote_moongen_throughput, local_moongen_throughput)
                    loadgen.copy_from(remote_moongen_histogram, local_moongen_histogram)

                    # summarize results of VM
                    with open(local_summary_file, 'w') as file:
                        try:
                            # to_string preserves all cols
                            summary = ipt.summarize(repetition).to_string()
                        except Exception as e:
                            summary = str(e)
                        file.write(summary) 


    # for ipt in all_tests:
    #     for repetition in range(repetitions):
    #         ipt.find_error(repetition)


if __name__ == "__main__":
    measurement = Measurement()
    main(measurement)
