from measure import Measurement, end_foreach, AbstractBenchTest
from dataclasses import dataclass, field, asdict
from logging import (info, debug, error, warning,
                     DEBUG, INFO, WARN, ERROR)
from server import Host, Guest, LoadGen, MultiHost
from enums import Machine, Interface, Reflector
from typing import Iterator, cast, List, Dict, Callable, Tuple, Any
import time
from pathlib import Path
import pandas as pd
from pandas import DataFrame

OUT_DIR: str
BRIEF: bool
DURATION_S: int

@dataclass
class YcsbTest(AbstractBenchTest):
    rps: int # requests per second
    interface: str # network interface used

    def test_infix(self):
        return f"ycsb_{self.num_vms}vms_{self.interface}_{self.rps}rps"

    def output_path_per_vm(self, repetition: int, vm_number: int) -> str:
        return str(Path(OUT_DIR) / f"ycsbVMs_{self.test_infix()}_rep{repetition}" / f"vm{vm_number}.log")

    def estimated_runtime(self) -> float:
        """
        estimate time needed to run this benchmark excluding boottime in seconds
        """
        app_setup = 3 # diff "running YcsbTest" "Running wrk2 x times"

        # repetition contains 
        # DURATION_S 
        # + wait_for_results(0 in underload, 10s in overload)
        # + 3s other overhead
        measurements = self.repetitions * ( DURATION_S + 13 )

        return measurements + app_setup

    def summarize(self, repetition: int) -> DataFrame:
        data = self.parse_results(repetition)
        denominators = ["repetitions", "rps", "interface", "num_vms", "op"]
        mean = data.groupby(denominators).mean(numeric_only=True)
        std = data.groupby(denominators).std(numeric_only=True)
        mean["summary"] = "mean"
        std["summary"] = "std"
        df = pd.concat([mean, std])
        del df["vm_number"]
        return df

    def parse_results(self, repetition: int) -> DataFrame:
        df = DataFrame()
        for vm_number in MultiHost.range(self.num_vms):
            df = pd.concat([df, self.parse_result(repetition, vm_number)])
        return df

    def parse_result(self, repetition: int, vm_number: int) -> DataFrame:
        def find(haystack: List[str], needle: str) -> str:
            matches = [line for line in haystack if needle in line ]
            if len(matches) != 1:
                raise Exception("Seemingly an error occured during execution")
            value = matches[0].split(" ")[-1]
            return value

        with open(self.output_path_per_vm(repetition, vm_number), 'r') as file:
            lines = file.readlines()
            data = []
            test_spec = {
                **asdict(self),
                "repetition": repetition,
                "vm_number": vm_number,
            }
            data += [{
                **test_spec,
                "op": "read",
                "avg_us": float(find(lines, "[READ], AverageLatency(us),")),
                "95th_us": float(find(lines, "[READ], 95thPercentileLatency(us),")),
                "99th_us": float(find(lines, "[READ], 99thPercentileLatency(us),")),
                "ops": int(find(lines, "[READ], Operations,"))
            }]
            data += [{
                **test_spec,
                "op": "update",
                "avg_us": float(find(lines, "[UPDATE], AverageLatency(us),")),
                "95th_us": float(find(lines, "[UPDATE], 95thPercentileLatency(us),")),
                "99th_us": float(find(lines, "[UPDATE], 99thPercentileLatency(us),")),
                "ops": int(find(lines, "[UPDATE], Operations,"))
            }]
            data += [{
                **test_spec,
                "op": "overall",
                "runtime": float(find(lines, "[OVERALL], RunTime(ms),")),
                "ops_per_sec": float(find(lines, "[OVERALL], Throughput(ops/sec),"))
            }]
            return DataFrame(data=data)




class Ycsb():
    def __init__(self, moonprogs_dir: str):
        pass

    def run(self, host: Host, loadgen: LoadGen, guests: Dict[int, Guest], test: YcsbTest):
        loadgen.stop_redis() # cleanup
        loadgen.start_redis()

        # load test data
        loadgen.exec(f"sudo rm /tmp/ycsb-load.log || true")
        loadgen.stop_ycsb() # cleanup
        loadgen.start_ycsb("localhost", outfile="/tmp/ycsb-load.log", load=True)
        loadgen.wait_for_success(f'[[ $(tail -n 1 /tmp/ycsb-load.log) = *"AUTOTEST_DONE"* ]]')
        loadgen.stop_ycsb()

        info(f"Running ycsb {test.repetitions} times")

        remote_output_file = "/tmp/ycsb-run.log"
        for repetition in range(test.repetitions):
            # time.sleep(5) # i dont think we need this
            # prepare test
            def foreach_parallel(i, guest): # pyright: ignore[reportGeneralTypeIssues]
                guest.exec(f"sudo rm {remote_output_file} || true")
            end_foreach(guests, foreach_parallel)

            # start test on all guests
            def foreach_parallel(i, guest): # pyright: ignore[reportGeneralTypeIssues]
                guest.start_ycsb("10.1.0.2", rate=test.rps, runtime=DURATION_S, threads=1, outfile=remote_output_file)
            end_foreach(guests, foreach_parallel)

            time.sleep(DURATION_S + 1)

            # collect test results
            def foreach_parallel(i, guest): # pyright: ignore[reportGeneralTypeIssues]
                local_per_vm_file = test.output_path_per_vm(repetition, i)
                try:
                    guest.wait_for_success(f'[[ $(tail -n 1 {remote_output_file}) = *"AUTOTEST_DONE"* ]]')
                except TimeoutError:
                    error('Waiting for output file to appear timed out. This can happen when the offered request rate is far beyond the processed rate.')
                guest.copy_from(remote_output_file, local_per_vm_file)
                guest.stop_ycsb() # cleanup
            end_foreach(guests, foreach_parallel)

            # summarize results of all VMs
            local_output_file = test.output_filepath(repetition)
            with open(local_output_file, 'w') as file:
                try:
                    # to_string preserves all cols
                    summary = test.summarize(repetition).to_string()
                except Exception as e:
                    summary = str(e)
                file.write(summary) 

        loadgen.stop_redis()
        pass
        

def main(measurement: Measurement, plan_only: bool = False) -> None:
    # general measure init
    host, loadgen = measurement.hosts()
    from measure import OUT_DIR as M_OUT_DIR, BRIEF as M_BRIEF
    global OUT_DIR
    global BRIEF
    global DURATION_S
    OUT_DIR = M_OUT_DIR
    BRIEF = M_BRIEF

    interfaces = [ Interface.VMUX_EMU, Interface.VMUX_DPDK, Interface.BRIDGE_E1000, Interface.BRIDGE ]
    rpsList = [ 10, 100, 500, 1000, 5000, 10000, 50000, 1000000 ]
    vm_nums = [ 1, 2, 4 ]
    repetitions = 3
    DURATION_S = 61 if not BRIEF else 11
    if BRIEF:
        # interfaces = [ Interface.BRIDGE_E1000 ]
        interfaces = [ Interface.VMUX_DPDK ]
        rpsList = [ 10 ]
        repetitions = 1
        vm_nums = [ 4 ]

    # test = YcsbTest(
    #         repetitions=1,
    #         rps=10,
    #         interface=Interface.BRIDGE_E1000.value,
    #         num_vms=2
    #         )
    # df = test.summarize()
    # breakpoint()

    # plan the measurement
    test_matrix = dict(
        repetitions=[ repetitions ],
        rps=rpsList,
        interface=[ interface.value for interface in interfaces],
        num_vms=vm_nums
        )
    info(f"YCSB execution plan:")
    YcsbTest.estimate_time(test_matrix, ["interface", "num_vms"])

    if plan_only:
        return

    for num_vms in vm_nums:
        
        moonprogs_dir = f"{measurement.config['guest']['moonprogs_dir']}"
        ycsb = Ycsb(moonprogs_dir)

        for interface in interfaces:

            # skip VM boots if possible
            test_matrix = dict(
                interface=[ interface.value ],
                rps=rpsList,
                repetitions=[ repetitions ],
                num_vms=[ num_vms ]
                )
            if not YcsbTest.any_needed(test_matrix):
                warning(f"Skipping num_vms {num_vms}: All measurements done already.")
                continue

            # boot VMs
            with measurement.virtual_machines(interface, num=num_vms) as guests:
                # loadgen: set up interfaces and networking

                debug('Binding loadgen interface')
                loadgen.modprobe_test_iface_drivers()
                loadgen.release_test_iface() # bind linux driver
                try:
                    loadgen.delete_nic_ip_addresses(loadgen.test_iface)
                except Exception:
                    pass
                loadgen.setup_test_iface_ip_net()

                def foreach_parallel(i, guest): # pyright: ignore[reportGeneralTypeIssues]
                    guest.setup_test_iface_ip_net()
                end_foreach(guests, foreach_parallel)
                
                for rps in rpsList:
                    # the actual test
                    test = YcsbTest(
                            repetitions=repetitions,
                            rps=rps,
                            interface=interface.value,
                            num_vms=num_vms
                            )
                    if test.needed():
                        info(f"running {test}")
                        ycsb.run(host, loadgen, guests, test)
                    else:
                        info(f"skipping {test}")


    YcsbTest.find_errors(OUT_DIR, ["READ-ERROR", "WRITE-ERROR"])


if __name__ == "__main__":
    measurement = Measurement()
    main(measurement)
