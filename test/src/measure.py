from dataclasses import dataclass
from server import Host, Guest, LoadGen, MultiHost
import autotest as autotest
from configparser import ConfigParser
from argparse import (ArgumentParser, ArgumentDefaultsHelpFormatter, Namespace,
                      FileType, ArgumentTypeError)
from typing import Tuple, Iterator, Any, Dict
from contextlib import contextmanager
from loadlatency import Interface, Machine, LoadLatencyTest, Reflector
from logging import (info, debug, error, warning,
                     DEBUG, INFO, WARN, ERROR)
from util import safe_cast
from pathlib import Path
import copy


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


def setup_host_interface(host: Host, interface: Interface, vm_number: int = 0) -> None:
    autotest.LoadLatencyTestGenerator.setup_interface(host, Machine.PCVM, interface, vm_number=vm_number)


@dataclass
class Measurement:
    args: Namespace
    config: ConfigParser

    host: Host
    guest: Guest
    loadgen: LoadGen

    guests: Dict[int, Guest] 


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


    def hosts(self) -> Tuple[Host, LoadGen]:
        return (self.host, self.loadgen)


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
    def virtual_machines(self, interface: Interface, num: int = 1) -> Iterator[Dict[int, Guest]]:

        # host: inital cleanup

        debug('Initial cleanup')
        try:
            self.host.kill_guest()
        except Exception:
            pass
        self.host.cleanup_network()


        # host: set up interfaces and networking

        self.host.detect_test_iface()

        for i in MultiHost.range(num):
            debug(f"Setting up interface {interface.value} for VM {i}")
            setup_host_interface(self.host, interface, vm_number=i)

        if interface in [ Interface.VMUX_PT, Interface.VMUX_EMU ]:
            self.host.start_vmux(interface.value)

        # start VM

        
        for i in MultiHost.range(num):
            info(f"Starting VM {i} ({interface.value})")

            self.host.run_guest(
                    net_type=interface.net_type(),
                    machine_type='pc',
                    qemu_build_dir="/scratch/okelmann/vmuxIO/qemu/bin",
                    vm_number=i
                    )

            if num == 1:
                self.guests[0] = self.guest
            else:
                # TODO move clone to Server or Guest
                guest = copy.deepcopy(self.guest)
                fqdn = guest.fqdn.split(".")
                fqdn[0] = f"{fqdn[0]}{i}"
                guest.fqdn = ".".join(fqdn)
                self.guests[i] = guest

        breakpoint()
        for i in MultiHost.range(num):
            debug("Waiting for guest{num} connectivity")
            self.guests[i].wait_for_connection(timeout=120)

        yield self.guests

        # teardown  # TODO multihost

        self.host.kill_guest()
        if interface in [ Interface.VMUX_PT, Interface.VMUX_EMU ]:
            self.host.stop_vmux()
        self.host.cleanup_network()




