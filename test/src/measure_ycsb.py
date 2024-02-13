from measure import Measurement, end_foreach, AbstractBenchTest
from dataclasses import dataclass, field
from logging import (info, debug, error, warning,
                     DEBUG, INFO, WARN, ERROR)
from loadlatency import Interface, Machine, LoadLatencyTest, Reflector
from server import Host, Guest, LoadGen, MultiHost
from typing import Iterator, cast, List, Dict, Callable, Tuple, Any
import time

OUT_DIR: str
BRIEF: bool

@dataclass
class YcsbTest(AbstractBenchTest):
    rps: int # requests per second
    interface: str # network interface used
    num_vms: int

    def test_infix(self):
        return f"ycsb_{self.num_vms}vms_{self.interface}_{self.rps}rps"


class Ycsb():
    def __init__(self, moonprogs_dir: str):
        pass

    def run(self, host: Host, loadgen: LoadGen, guests: Dict[int, Guest], test: YcsbTest):
        loadgen.stop_redis() # cleanup
        loadgen.start_redis()

        # load test data
        loadgen.exec(f"sudo rm /tmp/ycsb-load.log || true")
        loadgen.stop_ycsb() # cleanup
        loadgen.start_ycsb("localhost", 0, outfile="/tmp/ycsb-load.log", load=True)
        loadgen.wait_for_success(f'[[ $(tail -n 1 /tmp/ycsb-load.log) = *"AUTOTEST_DONE"* ]]')
        loadgen.stop_ycsb()

        info(f"Running ycsb {test.repetitions} times")

        remote_output_file = "/tmp/ycsb-run.log"
        exptected_duration_s = 1
        for repetition in range(test.repetitions):
            local_output_file = test.output_filepath(repetition)
            guests[1].stop_ycsb() # cleanup
            guests[1].exec(f"sudo rm {remote_output_file} || true")
            guests[1].start_ycsb("10.1.0.2", test.rps, threads=1, outfile=remote_output_file)
            time.sleep(exptected_duration_s + 1)
            try:
                guests[1].wait_for_success(f'[[ $(tail -n 1 {remote_output_file}) = *"AUTOTEST_DONE"* ]]')
            except TimeoutError:
                error('Waiting for output file to appear timed out')
            guests[1].copy_from(remote_output_file, local_output_file)
            pass
        loadgen.stop_redis()
        pass
        

def main() -> None:
    # general measure init
    measurement = Measurement()
    host, loadgen = measurement.hosts()
    from measure import OUT_DIR as M_OUT_DIR, BRIEF as M_BRIEF
    global OUT_DIR
    global BRIEF
    OUT_DIR = M_OUT_DIR
    BRIEF = M_BRIEF

    interfaces = [ Interface.VMUX_EMU, Interface.BRIDGE_E1000, Interface.BRIDGE ]
    rpsList = [ 10, 100, 200, 300, 400, 500, 600 ]
    vm_nums = [ 1 ]
    repetitions = 4
    if BRIEF:
        interfaces = [ Interface.BRIDGE_E1000 ]
        # interfaces = [ Interface.VMUX_EMU ]
        rpsList = [ 10 ]
        # apps = [ "hotelReservation" ]
        # apps = [ "socialNetwork" ]
        apps = [ "mediaMicroservices" ]
        repetitions = 1

    for num_vms in vm_nums:
        
        moonprogs_dir = f"{measurement.config['guest']['moonprogs_dir']}"
        ycsb = Ycsb(moonprogs_dir)

        test_matrix = dict(
            interface=[ interface.value for interface in interfaces],
            rps=rpsList,
            repetitions=[ repetitions ],
            num_vms=[ num_vms ]
            )
        if not YcsbTest.any_needed(test_matrix):
            warning(f"Skipping num_vms {num_vms}: All measurements done already.")
            continue

        for interface in interfaces:
            with measurement.virtual_machines(interface, num=num_vms) as guests:
                try:
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

                except Exception as e:
                    print(f"foo {e}")
                    breakpoint()
                    print("...")

    YcsbTest.find_errors(OUT_DIR, ["READ-ERROR", "WRITE-ERROR"])


if __name__ == "__main__":
    main()
