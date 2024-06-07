from dataclasses import dataclass
from util import safe_cast, product_dict
from typing import Iterator, cast, List, Dict, Callable, Tuple, Any
from os.path import isfile, join as path_join
from abc import ABC, abstractmethod
from server import Host, Guest, LoadGen, MultiHost
import autotest as autotest
from configparser import ConfigParser
from argparse import (ArgumentParser, ArgumentDefaultsHelpFormatter, Namespace,
                      FileType, ArgumentTypeError)
from typing import Tuple, Iterator, Any, Dict, Callable
from contextlib import contextmanager
from enums import Machine, Interface, Reflector
from logging import (info, debug, error, warning,
                     DEBUG, INFO, WARN, ERROR)
from util import safe_cast
from pathlib import Path
import copy
import time
from concurrent.futures import ThreadPoolExecutor
import subprocess
from root import QEMU_BUILD_DIR


OUT_DIR: str = "/tmp/out1"
BRIEF: bool = False
NUM_WORKERS: int = 8 # with 16, it already starts failing on rose


def setup_parser() -> ArgumentParser:
    """
    Creates ArgumentParser instance for parsing program options
    """

    # create the argument parser
    parser = ArgumentParser(
        description='''
        Foobar.''',
        formatter_class=ArgumentDefaultsHelpFormatter,
    )

    # define all the arguments
    parser.add_argument('-c',
                        '--config',
                        default='./autotest.cfg',
                        type=FileType('r'),
                        help='Configuration file path',
                        )
    parser.add_argument('-o',
                        '--outdir',
                        default=OUT_DIR,
                        help='Directory to expect old results in and write new ones to',
                        )
    parser.add_argument('-b',
                        '--brief',
                        action='store_true',
                        help='Use test parameters that take less time.',
                        )
    parser.add_argument('-v',
                        '--verbose',
                        dest='verbosity',
                        action='count',
                        default=0,
                        help='''Verbosity, can be given multiple times to set
                             the log level (0: error, 1: warn, 2: info, 3:
                             debug)''',
                        )
    return parser


def setup_host_interface(host: Host, interface: Interface, vm_range: range = range(0)) -> None:
    autotest.LoadLatencyTestGenerator.setup_interface(host, Machine.PCVM, interface, vm_range=vm_range)


def end_foreach(guests: Dict[int, Guest], func: Callable[[int, Guest], None], workers: int = NUM_WORKERS):
    """
    Example usage:
        def foreach_parallel(i, guest): # pyright: ignore[reportGeneralTypeIssues]
            guest.exec(f"docker-compose up")
        end_foreach(guests, foreach_parallel)
    """
    with ThreadPoolExecutor(max_workers=workers) as executor:
        futures = [executor.submit(func, i, guest) for i, guest in guests.items()]
        for future in futures:
            future.result()  # Waiting for all tasks to complete


@dataclass
class Measurement:
    args: Namespace
    config: ConfigParser

    host: Host
    guest: Guest
    loadgen: LoadGen

    guests: Dict[int, Guest]  # for multihost VMs we start counting VMs at 1


    def __init__(self):
        parser: ArgumentParser = setup_parser()
        self.args: Namespace = autotest.parse_args(parser)
        autotest.setup_logging(self.args)
        self.config: ConfigParser = autotest.setup_and_parse_config(self.args)

        host, guest, loadgen = autotest.create_servers(self.config).values()
        self.host = safe_cast(Host, host)
        self.guest = safe_cast(Guest, guest)
        self.loadgen = safe_cast(LoadGen, loadgen)

        self.host.check_cpu_freq()
        self.loadgen.check_cpu_freq()

        self.guests = dict()

        global BRIEF
        global OUT_DIR
        BRIEF = self.args.brief
        OUT_DIR = self.args.outdir

        if BRIEF:
            subprocess.run(["sudo", "rm", "-r", OUT_DIR])


    def hosts(self) -> Tuple[Host, LoadGen]:
        return (self.host, self.loadgen)


    @staticmethod
    def estimated_boottime(num_vms: int) -> float:
        """
        estimated time to boot this system in seconds
        """
        vm_boot = (num_vms / 32) * 60 * 2 # time taken to boot a batch of VMs
        return vm_boot


    @contextmanager
    def virtual_machine(self, interface: Interface) -> Iterator[Guest]:
        """
        Creates a guest virtual machine
        """

        # host: inital cleanup

        debug('Initial cleanup')
        try:
            self.host.kill_guest()
        except Exception:
            pass

        self.host.cleanup_network()

        # host: set up interfaces and networking

        self.host.detect_test_iface()

        debug(f"Setting up interface {interface.value}")
        setup_host_interface(self.host, interface)

        if interface.needs_vmux():
            self.host.start_vmux(interface)

        # start VM

        info(f"Starting VM ({interface.value})")
        self.host.run_guest(
                net_type=interface,
                machine_type='pc',
                qemu_build_dir=QEMU_BUILD_DIR
                )

        debug("Waiting for guest connectivity")
        self.guest.wait_for_connection(timeout=120)

        yield self.guest

        # teardown

        self.host.kill_guest()
        if interface.needs_vmux():
            self.host.stop_vmux()
        self.host.cleanup_network()


    @contextmanager
    def virtual_machines(self, interface: Interface, num: int = 1, batch: int = 32) -> Iterator[Dict[int, Guest]]:
        # Batching is necessary cause if network setup takes to long, bringing up the interfaces times out, networking is permanently broken and systemds spiral into busy restarting

        # host: inital cleanup

        debug('Initial cleanup')

        try:
            self.host.kill_guest()

        except Exception as e:
            error("Could not kill guest VMs")
            error(e.with_traceback())
            pass

        self.host.modprobe_test_iface_drivers() # cleanup network needs device drivers
        self.host.cleanup_network()


        # host: set up interfaces and networking

        self.host.detect_test_iface()

        unbatched_interfaces = [ 
            interface for interface in Interface.__members__.values() if 
                                interface.needs_vfio() or (interface.needs_br_tap() and interface.needs_vmux()) ]
        
        if interface in unbatched_interfaces:
            # vmux taps need to be there all from the start (no batching)
            debug(f"Setting up interface {interface.value} for {num} VMs")
            setup_host_interface(self.host, interface, vm_range=range(1, num+1))

        if interface.needs_vmux():
            self.host.start_vmux(interface, num_vms=num)

        # start VMs in batches of batch
        range_ = MultiHost.range(num)

        while range_:
            # pop first batch
            vm_range = range_[:batch]
            range_ = range_[batch:]

            info(f"Starting VM {vm_range.start}-{vm_range.stop - 1}")
   
            if interface not in unbatched_interfaces:
                debug(f"Setting up interface {interface.value} for {num} VMs")
                setup_host_interface(self.host, interface, vm_range=vm_range)

            # start VM

            for i in vm_range:
                info(f"Starting VM {i} ({interface.value})")

                # self.host.run_guest(net_type=interface.net_type(), machine_type='pc', qemu_build_dir=QEMU_BUILD_DIR, vm_number=81)
                self.host.run_guest(
                        net_type=interface,
                        machine_type='pc',
                        qemu_build_dir=QEMU_BUILD_DIR,
                        vm_number=i
                        )

                self.guests[i] = self.guest.multihost_clone(i)

                # giving each qemu instance time to allocate its memory can help starting more intances. 
                # If multiple qemus allocate at the same time, both will fail even though one could have successfully started if it was the only one doing allocations. 
                time.sleep(1) 

            info(f"Waiting for connectivity of guests")
            for i in vm_range:
                self.guests[i].wait_for_connection(timeout=120)
            
        yield self.guests

        # teardown

        self.host.kill_guest()
        if interface in [ Interface.VMUX_PT, Interface.VMUX_EMU ]:
            self.host.stop_vmux()
        self.host.cleanup_network()


@dataclass
class AbstractBenchTest(ABC):
    # magic parameter: repetitions
    # Feature matrixes for other parameters take lists of values to iterate over. 
    # Wile repetitions is also a list in the feature matrix, even single value (e.g. 3) still means that this test will be run 3 times.
    repetitions: int 
    num_vms: int

    @abstractmethod
    def test_infix(self) -> str:
        # example: return f"{self.app}_{self.interface}_{self.num_vms}vms_{self.rps}rps"
        raise Exception("unimplemented")

    @abstractmethod
    def estimated_runtime(self) -> float:
        """
        estimate time needed to run this benchmark excluding boottime in seconds. Return -1 if unknown.
        """
        raise -1

    def output_filepath(self, repetition: int, extension: str = "log"):
        return path_join(OUT_DIR, f"{self.test_infix()}_rep{repetition}.{extension}")

    def test_done(self, repetition: int):
        output_file = self.output_filepath(repetition)
        return isfile(output_file)

    def needed(self):
        for repetition in range(self.repetitions):
            if not self.test_done(repetition):
                return True
        return False

    @classmethod
    def list_tests(cls, test_matrix: Dict[str, List[Any]]) -> List[Any]:
        """
        test_matrix:
        dict where every key corresponds to a constructor parameter/field for this cls/BenchTest (same list as test_parameters() returns).
        Each value is a list of values to be tested.
        """
        ret = []
        for test_args in product_dict(test_matrix):
            test = cls(**test_args)
            ret += [ test ]
        return ret

    @classmethod
    def any_needed(cls, test_matrix: Dict[str, List[Any]]) -> bool:
        """
        Test matrix: like kwargs used to initialize DeathStarBenchTest, but every value is the list of values.
        """
        for test in cls.list_tests(test_matrix):
            if test.needed():
                debug(f"any_needed: needs {test}")
                return True
        return False

    @staticmethod
    def find_errors(out_dir: str, error_indicators: List[str]) -> bool:
        """
        See if output files match any error_indicator strings
        """
        failure = False
        out = ""
        try:
            for match in error_indicators:
                out = out + "\n" + str(subprocess.check_output(["grep", "-r", match, out_dir]))
        except subprocess.CalledProcessError:
            failure = True

        errors_found = not failure
        if errors_found:
            error(f"Some wrk2 tests returned errors:\n{out}")
        return errors_found

    @classmethod
    def estimate_time(cls, test_matrix: Dict[str, List[Any]], args_reboot: List[str]) -> float:
        """
        Test matrix: like kwargs used to initialize DeathStarBenchTest, but every value is the list of values.
        kwargs_reboot: test_matrix keys. Changing these parameters requires a reboot.
        """
        total = 0
        needed = 0
        needed_s = 0.0
        unknown_runtime = False
        for test_args in product_dict(test_matrix):
            test = cls(**test_args)
            total += 1
            if test.needed():
                needed += 1
                estimated_runtime = test.estimated_runtime()
                if estimated_runtime == -1:
                    unknown_runtime = True
                needed_s += estimated_runtime

        # args not relevant for reboots
        args_no_reboot = [ key for key in test_matrix.keys() if key not in args_reboot ]
        # example (1st) test parameters not relevant for reboots
        kwargs_no_reboot = {key: test_matrix[key][0] for key in args_no_reboot }
        # test parameters relevant for reboots:
        reboot_matrix = {key: value for key, value in test_matrix.items() if key in args_reboot}
        # loop over all reboots
        for test_args in product_dict(reboot_matrix):
            test_args = {**test_args, **kwargs_no_reboot}
            test = cls(**test_args)
            duration_s = Measurement.estimated_boottime(test.num_vms)
            if "repetitions" in args_reboot:
                duration_s *= test.repetitions
            needed_s += duration_s

        if unknown_runtime:
            info(f"Test not cached yet: {needed}/{total}. Time to completion not known.")
        else:
            info(f"Test not cached yet: {needed}/{total}. Expected time to completion: {needed_s/60:.0f}min ({needed_s/60/60:.1f}h)")

        return needed_s

    @classmethod
    def test_parameters(cls) -> List[str]:
        """
        return the list of names of input parameters for this test
        """
        return [i for i in cls.__match_args__]


import measure_vnf
import measure_hotel
import measure_ycsb

def main():
    measurement = Measurement()

    # estimate runtimes
    info("")
    measure_vnf.main(measurement, plan_only=True)
    info("")
    measure_hotel.main(measurement, plan_only=True)
    info("")
    measure_ycsb.main(measurement, plan_only=True)
    info("")

    info("Running benchmarks ...")
    info("")
    measure_vnf.main(measurement)
    measure_hotel.main(measurement)
    measure_ycsb.main(measurement)

if __name__ == "__main__":
    main()
