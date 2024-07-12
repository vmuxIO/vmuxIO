from dataclasses import dataclass
from util import safe_cast, product_dict
from typing import Iterator, cast, List, Dict, Callable, Tuple, Any, Self, TypeVar, Iterable, Type, Generic
from os.path import isfile, join as path_join
import os
from abc import ABC, abstractmethod
from server import Host, Guest, LoadGen
import autotest as autotest
from configparser import ConfigParser
from argparse import (ArgumentParser, ArgumentDefaultsHelpFormatter, Namespace,
                      FileType, ArgumentTypeError)
from typing import Tuple, Iterator, Any, Dict, Callable
from contextlib import contextmanager, ContextDecorator
from enums import Machine, Interface, Reflector, MultiHost
from logging import (info, debug, error, warning,
                     DEBUG, INFO, WARN, ERROR)
from util import safe_cast, deduplicate
from pathlib import Path
import copy
import time
from concurrent.futures import ThreadPoolExecutor
import subprocess
from root import QEMU_BUILD_DIR
from conf import G
from tqdm import tqdm
from tqdm.contrib.telegram import tqdm as tqdm_telegram

NUM_WORKERS = 8 # with 16, it already starts failing on rose

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
                        default='/tmp/out1',
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

        G.OUT_DIR = self.args.outdir
        G.BRIEF = self.args.brief

        if G.BRIEF:
            subprocess.run(["sudo", "rm", "-r", G.OUT_DIR])


    def hosts(self) -> Tuple[Host, LoadGen]:
        return (self.host, self.loadgen)


    @staticmethod
    def estimated_boottime(num_vms: int) -> float:
        """
        estimated time to boot this system in seconds
        """
        # time taken to boot a batch of VMs (2min each)
        # min(factor for 1 VM, factor for more VMs)
        vm_boot = max(0.3, (num_vms / 32)) * 60 * 2
        return vm_boot


    @contextmanager
    def virtual_machine(self, interface: Interface, run_guest_args = dict()) -> Iterator[Guest]:
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
                qemu_build_dir=QEMU_BUILD_DIR,
                **run_guest_args
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
        self.guests = dict()

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


A = TypeVar("A", bound='AbstractBenchTest')

@dataclass
class AbstractBenchTest(ABC):
    # magic parameter: repetitions
    # Feature matrixes for other parameters take lists of values to iterate over.
    # Wile repetitions is also a list in the feature matrix, even single value (e.g. 3) still means that this test will be run 3 times.
    repetitions: int
    num_vms: int # assumed to always be in args_reboot

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
        return path_join(G.OUT_DIR, f"{self.test_infix()}_rep{repetition}.{extension}")

    def test_done(self, repetition: int):
        output_file = self.output_filepath(repetition)
        return isfile(output_file)

    def needed(self):
        for repetition in range(self.repetitions):
            if not self.test_done(repetition):
                return True
        return False

    @classmethod
    def list_tests(cls: Type[A], test_matrix: Dict[str, List[Any]], exclude_test = lambda test: False) -> List[A]:
        """
        test_matrix:
        dict where every key corresponds to a constructor parameter/field for this cls/BenchTest (same list as test_parameters() returns).
        Each value is a list of values to be tested.
        """
        ret = []
        for test_args in product_dict(test_matrix):
            test = cls(**test_args)
            if not exclude_test(test):
                ret += [ test ]
        return ret

    @classmethod
    def test_matrix(cls: Type[A], tests: List[A]):
        """
        Generalizes a list of tests into a test matrix.
        Note, that this matrix can be a superset of the original list of tests.
        This can happen for example, if the list is a filtered version of list_tests(), because this sparseness cannot be represented by a test_matrix.
        """
        if len(tests) == 0:
            return dict()

        ret = dict()
        for key in tests[0].__dict__.keys():
            values = []
            for test in tests:
                value = test.__dict__[key]
                if value not in values:
                    values += [ value ]
            ret[key] = values

        return ret

    @classmethod
    def test_matrix_string(cls: Type[A], tests: List[A]) -> str:
        """
        Calls test_matrix() and formats the output nicely.
        """
        test_matrix = cls.test_matrix(tests)

        # calculate sparseness (how many entries in the test matrix are actually in tests)
        num_tests_matrix = 0
        for values in test_matrix.values():
            if num_tests_matrix == 0:
                num_tests_matrix = len(values)
            else:
                num_tests_matrix *= len(values)
        matrix_usage = len(tests) / num_tests_matrix
        sparseness = f"{(1 - matrix_usage)*100:.0f}%"

        dimensions = len(test_matrix.keys())
        if matrix_usage == 1:
            description = f"{dimensions} dimensional feature matrix: "
        else:
            description = f"{dimensions} dimensional feature matrix ({sparseness} sparse): "

        ret = description + "{ \n"
        for key, values in test_matrix.items():
            values = ", ".join([ str(v) for v in values])
            ret += f"  {key}: {values}\n"
        return ret + "}"

    @classmethod
    def any_needed(cls, test_matrix: Dict[str, List[Any]], exclude_test = lambda test: False) -> bool:
        """
        Test matrix: like kwargs used to initialize DeathStarBenchTest, but every value is the list of values.
        """
        for test in cls.list_tests(test_matrix, exclude_test=exclude_test):
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
    def estimate_time(cls, test_matrix: Dict[str, List[Any]], args_reboot: List[str] = []) -> float:
        """
        Test matrix: like kwargs used to initialize DeathStarBenchTest, but every value is the list of values.
        kwargs_reboot: test_matrix keys. Changing these parameters requires a reboot.
        """
        tests = cls.list_tests(test_matrix)
        return cls.estimate_time2(tests, args_reboot=args_reboot, prints=True)

    @classmethod
    def estimate_time2(cls, tests: List[Self], args_reboot: List[str], prints: bool = True) -> float:
        """
        Test matrix: like kwargs used to initialize DeathStarBenchTest, but every value is the list of values.
        kwargs_reboot: test_matrix keys. Changing these parameters requires a reboot.
        tests: if not None, ignore test_matrix
        """
        # usually we check if a test has already been run. Hence we never run the same test twice.
        tests = deduplicate(tests)

        total = 0
        needed = 0
        needed_s = 0.0
        unknown_runtime = False
        tests_needed = []
        for test in tests:
            total += 1
            if test.needed():
                needed += 1
                estimated_runtime = test.estimated_runtime()
                if estimated_runtime == -1:
                    unknown_runtime = True
                needed_s += estimated_runtime
                tests_needed += [ test ]

        # find test parameters that trigger reboots
        reboot_triggers = []
        reboot_tests = []
        for test in tests_needed:
            # only test parameters relevant for reboots
            test_parameters = dict()
            for key in args_reboot:
                test_parameters[key] = test.__dict__[key]
            # append only the first test that triggeres a reboot
            if test_parameters not in reboot_triggers:
                reboot_triggers += [ test_parameters ]
                reboot_tests += [ test ]

        # estimate time needed for reboots
        for trigger, test in zip(reboot_triggers, reboot_tests):
            duration_s = Measurement.estimated_boottime(test.num_vms)
            if "repetitions" in args_reboot:
                duration_s *= test.repetitions
            needed_s += duration_s

        if prints:
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

T = TypeVar("T", bound=AbstractBenchTest)

class Bench(Generic[T], ContextDecorator):
    tqdm: Any

    def __init__(self, tests: List[T], args_reboot: List[str] = [], brief: bool = False):
        self.args_reboot = args_reboot
        assert len(tests) > 0
        self.remaining_tests = [ test for test in tests if test.needed()]
        self.time_remaining_s = AbstractBenchTest.estimate_time2(self.remaining_tests, args_reboot=self.args_reboot, prints=False)
        self.tqdm_constructor = tqdm
        self.brief = brief
        self.probe_peter()

    def probe_peter(self):
        if not self.brief:
            try:
                runtime_dir = os.environ["XDG_RUNTIME_DIR"]
                with open(f"{runtime_dir}/telegram_bot_token", 'r') as file:
                    data = file.read().strip()
                def my_tqdm(*args, **kwargs):
                    return tqdm_telegram(*args, chat_id="272730663", token=data, **kwargs)
                self.tqdm_constructor = my_tqdm
            except Exception:
                pass # leave default tqdm_constructor

    def __enter__(self):
        total_min = (self.time_remaining_s + 0.1) / 60 # +0.1 to avoid float summing errors
        self.tqdm = self.tqdm_constructor(total=total_min,
              unit="min",
              # - cut off float decimals
              # - append newline to print every update to new line (to avoid garbled newlines when we print between progress updates)
              bar_format="{l_bar}{bar}| {n:.0f}/{total:.0f}min [{elapsed}<{remaining}, {rate_fmt}{postfix}]\n",
              )
        return self, self.remaining_tests

    def __exit__(self, *exc):
        self.tqdm.close()

    def iterator(self, tests: List[T], test_parameter: str) -> Iterable[Tuple[Any, List[T]]]:
        """
        Return an iterator over all values of test_parameter of tests.
        test_parameter: name of the parameter (AbstractBenchTest member variable name) to iterate over
        """
        parameter_values = [ test.__dict__[test_parameter] for test in tests ]
        parameter_values = deduplicate(parameter_values)
        for parameter_value in parameter_values:
            parameter_tests = [ test for test in tests if test.__dict__[test_parameter] == parameter_value]
            yield (parameter_value, parameter_tests)


    def multi_iterator(self, tests: List[T], test_parameters: List[str]) -> Iterable[Tuple[List[Any], List[T]]]:
        """
        Loops over test_parameters (member variables in tests[] objects).

        Like multi_iterator(), but can be used like:
        for [a_param, b_param], tests_ in bench.multi_iterator(tests, ["a", "b"]):
            ...
        """
        for iteration_parameter_dict, iteration_tests in self.multi_iterator_dict(tests, test_parameters):
            iteration_parameters = [ iteration_parameter_dict[parameter_name] for parameter_name in test_parameters ]
            yield (iteration_parameters, iteration_tests)


    def multi_iterator_dict(self, tests: List[T], test_parameters: List[str]) -> Iterable[Tuple[Dict[str, Any], List[T]]]:
        """
        Can replace nested iterators.

        Example:
        for param_a, a_tests in bench.iterator(tests, "a"):
            for param_b, b_tests in bench.iterator(a_tests, "b"):
                ...
        Replaced with:
        for params, tests_ in bench.multi_iterator(tests, ["a", "b"]):
            param_a = params["a"]
            param_b = params["b"]
            ...
        """
        if len(tests) == 0:
            return

        test_matrix = tests[0].test_matrix(tests)
        loop_matrix = { key: value for (key, value) in test_matrix.items() if key in test_parameters }
        # from the potential feature matrix, generate a list of all possible parameter combinations
        potential_iterations = product_dict(loop_matrix)
        for iteration_parameters in potential_iterations:
            # find all tests that match the parameters of this iteration
            iteration_tests = [ test for test in tests if all(map(lambda kv: test.__dict__[kv[0]] == kv[1], iteration_parameters.items())) ]
            if len(iteration_tests) == 0:
                # can happen if the original list does not contain all tests generated by a full feature matrix
                continue
            yield (iteration_parameters, iteration_tests)

    def done(self, done: T):
        # TODO make this obsolete: integrate it into iterators and __exit__ or so
        self.remaining_tests = [ test for test in self.remaining_tests if test != done ]
        time_remaining_new_s = AbstractBenchTest.estimate_time2(self.remaining_tests, args_reboot=self.args_reboot, prints=False)
        time_progress_s = self.time_remaining_s - time_remaining_new_s
        self.time_remaining_s = time_remaining_new_s
        self.tqdm.update(time_progress_s / 60)


import measure_vnf
import measure_hotel
import measure_ycsb
import measure_iperf
import measure_mediation

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
    measure_iperf.main(measurement, plan_only=True)
    info("")
    measure_mediation.main(measurement, plan_only=True)
    info("")

    info("Running benchmarks ...")
    info("")
    # measure_vnf.main(measurement)
    measure_ycsb.main(measurement)
    measure_iperf.main(measurement)
    measure_mediation.main(measurement)
    measure_hotel.main(measurement)

if __name__ == "__main__":
    main()
