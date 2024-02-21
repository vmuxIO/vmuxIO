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
from loadlatency import Interface, Machine, LoadLatencyTest, Reflector
from logging import (info, debug, error, warning,
                     DEBUG, INFO, WARN, ERROR)
from util import safe_cast
from pathlib import Path
import copy
import time
from concurrent.futures import ThreadPoolExecutor
import subprocess


OUT_DIR: str = "/tmp/out1"
BRIEF: bool = False
NUM_WORKERS: int = 8 # with 16, it already starts failing on rose


def setup_parser() -> ArgumentParser:
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

        if interface in [ Interface.VMUX_PT, Interface.VMUX_EMU ]:
            self.host.start_vmux(interface.value)

        # start VM

        info(f"Starting VM ({interface.value})")
        self.host.run_guest(
                net_type=interface.net_type(),
                machine_type='pc',
                qemu_build_dir="/scratch/okelmann/vmuxIO/qemu/bin"
                )

        debug("Waiting for guest connectivity")
        self.guest.wait_for_connection(timeout=120)

        yield self.guest

        # teardown

        self.host.kill_guest()
        if interface in [ Interface.VMUX_PT, Interface.VMUX_EMU ]:
            self.host.stop_vmux()
        self.host.cleanup_network()


    @contextmanager
    def virtual_machines(self, interface: Interface, num: int = 1, batch: int = 32) -> Iterator[Dict[int, Guest]]:
        # Batching is necessary cause if network setup takes to long, bringing up the interfaces times out, networking is permanently broken and systemds spiral into busy restarting

        # host: inital cleanup

        debug('Initial cleanup')
        try:
            self.host.kill_guest()
        except Exception:
            pass
        self.host.modprobe_test_iface_drivers() # cleanup network needs device drivers
        self.host.cleanup_network()


        # host: set up interfaces and networking

        self.host.detect_test_iface()

        if interface == Interface.VMUX_EMU:
            # vmux taps need to be there all from the start (no batching)
            debug(f"Setting up interface {interface.value} for {num} VMs")
            setup_host_interface(self.host, interface, vm_range=range(1, num+1))

        if interface in [ Interface.VMUX_PT, Interface.VMUX_EMU ]:
            self.host.start_vmux(interface.value, num_vms=num)

        # start VMs in batches of batch
        range_ = MultiHost.range(num)
        while range_:
            # pop first batch
            vm_range = range_[:batch]
            range_ = range_[batch:]

            info(f"Starting VM {vm_range.start}-{vm_range.stop - 1}")
   
            if interface != Interface.VMUX_EMU:
                debug(f"Setting up interface {interface.value} for {num} VMs")
                setup_host_interface(self.host, interface, vm_range=vm_range)

            # start VM

            for i in vm_range:
                info(f"Starting VM {i} ({interface.value})")

                # self.host.run_guest(net_type=interface.net_type(), machine_type='pc', qemu_build_dir="/scratch/okelmann/vmuxIO/qemu/bin", vm_number=81)
                self.host.run_guest(
                        net_type=interface.net_type(),
                        machine_type='pc',
                        qemu_build_dir="/scratch/okelmann/vmuxIO/qemu/bin",
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
    repetitions: int

    @abstractmethod
    def test_infix(self) -> str:
        # example: return f"{self.app}_{self.interface}_{self.num_vms}vms_{self.rps}rps"
        return "error"

    def output_filepath(self, repetition: int):
        return path_join(OUT_DIR, f"{self.test_infix()}_rep{repetition}.log")

    def test_done(self, repetition: int):
        output_file = self.output_filepath(repetition)
        return isfile(output_file)

    def needed(self):
        for repetition in range(self.repetitions):
            if not self.test_done(repetition):
                return True
        return False

    @classmethod
    def any_needed(cls, test_matrix: Dict[str, List[Any]]) -> bool:
        """
        Test matrix: like kwargs used to initialize DeathStarBenchTest, but every value is the list of values.
        """
        for test_args in product_dict(test_matrix):
            test = cls(**test_args)
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


