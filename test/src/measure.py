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
import time


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


def setup_host_interface(host: Host, interface: Interface, vm_range: range = range(0)) -> None:
    autotest.LoadLatencyTestGenerator.setup_interface(host, Machine.PCVM, interface, vm_range=vm_range)


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
    
        # start VMs in batches of batch
        range_ = MultiHost.range(num)
        while range_:
            # pop first batch
            vm_range = range_[:batch]
            range_ = range_[batch:]

            info(f"Starting VM {vm_range.start}-{vm_range.stop - 1}")
    
            debug(f"Setting up interface {interface.value} for {num} VMs")
            setup_host_interface(self.host, interface, vm_range=vm_range) # TODO

            if interface in [ Interface.VMUX_PT, Interface.VMUX_EMU ]:
                self.host.start_vmux(interface.value)

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
                try:
                    self.guests[i].wait_for_connection(timeout=120)
                except Exception:
                    print("mh?")
                    breakpoint()
            
        yield self.guests

        # teardown

        self.host.kill_guest()
        if interface in [ Interface.VMUX_PT, Interface.VMUX_EMU ]:
            self.host.stop_vmux()
        self.host.cleanup_network()




