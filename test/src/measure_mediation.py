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
from pandas import DataFrame, read_csv, concat



@dataclass
class MediationTest(AbstractBenchTest):

    # test options
    interface: str # network interface used
    fastclick: str # one of: software | hardware

    def test_infix(self):
        return f"mediation_{self.fastclick}_{self.num_vms}vms_{self.interface}"

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
        time_sleeps = DURATION_S + 10 + 5
        overheads = 35
        return self.repetitions * (time_sleeps + overheads)

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

def run_test(host: Host, loadgen: LoadGen, guest: Guest, test: MediationTest, repetition: int):
    remote_fastclick_log = "/tmp/fastclick.log"
    local_fastclick_log = test.output_filepath_fastclick(repetition)
    remote_moongen_throughput = "/tmp/throughput.csv"
    local_moongen_throughput = test.output_filepath_throughput(repetition)
    remote_moongen_histogram = "/tmp/histogram.csv"
    local_moongen_histogram = test.output_filepath_histogram(repetition)
    guest.exec(f"sudo rm {remote_fastclick_log} || true")
    loadgen.exec(f"sudo rm {remote_moongen_throughput} || true")
    loadgen.exec(f"sudo rm {remote_moongen_histogram} || true")

    info("Starting Fastclick")
    if test.fastclick == "software":
        fastclick_program = "test/fastclick/mac-switch-software.click"
        fastclick_args = {
            'ifacePCI0': guest.test_iface_addr,
        }
    elif test.fastclick == "hardware":
        fastclick_program = "test/fastclick/mac-switch-hardware.click"
        project_root = f"{guest.moonprogs_dir}/../../"
        fastclick_rules = f"{project_root}/test/fastclick/test_dpdk_nic_rules"
        fastclick_rules = "/home/host/vmuxIO/test/fastclick/test_dpdk_nic_rules"
        fastclick_args = {
            'ifacePCI0': guest.test_iface_addr,
            'rules': fastclick_rules
        }
    else:
        raise Exception("Unknown fastclick program type")

    guest.start_fastclick(fastclick_program, remote_fastclick_log, script_args=fastclick_args)
    time.sleep(10) # fastclick takes roughly as long as moongen to start, be we give it some slack nevertheless
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
    fastclicks = [
            "hardware",
            "software",
            ]
    vm_nums = [ 1 ] # 2 VMs are currently not supported for VMUX_DPDK*
    repetitions = 9
    DURATION_S = 61 if not BRIEF else 11
    if BRIEF:
        # interfaces = [ Interface.BRIDGE_E1000 ]
        interfaces = [ Interface.VMUX_DPDK_E810 ]
        # interfaces = [ Interface.VFIO ]
        # fastclicks = [ "software" ]
        vm_nums = [ 1 ]
        repetitions = 6

    test_matrix = dict(
        repetitions=[ repetitions ],
        interface=[ interface.value for interface in interfaces],
        fastclick = fastclicks,
        num_vms=vm_nums
    )
    all_tests = MediationTest.list_tests(test_matrix)

    info(f"Iperf Test execution plan:")
    MediationTest.estimate_time(test_matrix, ["interface", "num_vms", "repetitions", "fastclick"])

    if plan_only:
        return

    tests_run = []

    for num_vms in vm_nums:
        for interface in interfaces:
            for fastclick in fastclicks:
                for repetition in range(repetitions):
                    # skip VM boots if possible
                    test_matrix = dict(
                        repetitions=[ repetitions ],
                        interface=[ interface.value ],
                        fastclick=[ fastclick ],
                        num_vms=[ num_vms ],
                        )
                    if not MediationTest.any_needed(test_matrix):
                        warning(f"Skipping {fastclick}@{interface}: All measurements done already.")
                        continue

                    # build test object
                    test = MediationTest(
                            interface=interface.value,
                            repetitions=repetitions,
                            fastclick=fastclick,
                            num_vms=num_vms)
                    tests_run += [ test ]
                    info(f"Testing repetition {repetition}: {test}")

                    info("Booting VM")
                    with measurement.virtual_machine(interface) as guest:
                        # loadgen: set up interfaces and networking

                        info('Binding loadgen interface')
                        loadgen.modprobe_test_iface_drivers()
                        loadgen.bind_test_iface() # bind vfio driver

                        # guest.modprobe_test_iface_drivers()
                        guest.bind_test_iface() # bind vfio driver

                        # run test
                        run_test(host, loadgen, guest, test, repetition)

                        # summarize results
                        local_summary_file = test.output_filepath(repetition)
                        with open(local_summary_file, 'w') as file:
                            try:
                                # to_string preserves all cols
                                summary = test.summarize(repetition)
                                summary = summary.to_string()
                            except Exception as e:
                                summary = str(e)
                            file.write(summary)


    # summarize all summaries
    all_summaries = []
    for test in all_tests:
        for repetition in range(test.repetitions):
            all_summaries += [ read_csv(test.output_filepath(repetition), sep='\\s+') ]
    df = concat(all_summaries).groupby(MediationTest.test_parameters())['rxMppsCalc'].describe()

    with open(path_join(OUT_DIR, f"mediation_summary.log"), 'w') as file:
        file.write(df.to_string())

    # for ipt in all_tests:
    #     for repetition in range(repetitions):
    #         ipt.find_error(repetition)


if __name__ == "__main__":
    measurement = Measurement()
    main(measurement)
