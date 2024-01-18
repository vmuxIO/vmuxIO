import autotest as autotest
from configparser import ConfigParser
from argparse import (ArgumentParser, ArgumentDefaultsHelpFormatter, Namespace,
                      FileType, ArgumentTypeError)
from argcomplete import autocomplete
from logging import (info, debug, error, warning,
                     DEBUG, INFO, WARN, ERROR)
from server import Host, Guest, LoadGen
from loadlatency import Interface, Machine

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


def setup_host_interface(host: Host, interface: Interface) -> None:
    autotest.LoadLatencyTestGenerator.setup_interface(host, Machine.PCVM, interface)


def do_measure() -> None:
    info("heureka")
    breakpoint()


def main() -> None:
    parser: ArgumentParser = setup_parser()
    args: Namespace = autotest.parse_args(parser)
    autotest.setup_logging(args)
    conf: ConfigParser = autotest.setup_and_parse_config(args)

    # measure:

    host, guest, loadgen = autotest.create_servers(conf).values()
    host: Host = host
    guest: Guest = guest
    loadgen: LoadGen = loadgen

    host.check_cpu_freq()
    loadgen.check_cpu_freq()

    debug('Initial cleanup')
    try:
        host.kill_guest()
    except Exception:
        pass
    host.cleanup_network()

    debug('Binding loadgen interface')
    try:
        loadgen.delete_nic_ip_addresses(loadgen.test_iface)
    except Exception:
        pass
    loadgen.bind_test_iface()
    loadgen.setup_hugetlbfs()

    host.detect_test_iface()

    interface = Interface.VMUX_PT

    debug(f"Setting up interface {interface.value}")
    setup_host_interface(host, interface)

    if interface in [ Interface.VMUX_PT, Interface.VMUX_EMU ]:
        host.start_vmux(interface.value)

    info("Starting VM")
    host.run_guest(
            net_type=Host.net_type(interface),
            machine_type='pc',
            qemu_build_dir="/scratch/okelmann/vmuxIO/qemu/bin"
            )
    # TODO maybe check if tmux session running

    debug("Waiting for guest connectivity")
    try:
        guest.wait_for_connection(timeout=120)
    except TimeoutError:
        error('Waiting for connection to guest ' +
              'timed out.')
        # TODO kill guest, teardown network,
        # recreate and retry
        return

    debug("Detecting guest test interface")
    guest.detect_test_iface()

    do_measure()

    host.kill_guest()
    if interface in [ Interface.VMUX_PT, Interface.VMUX_EMU ]:
        host.stop_vmux()
    host.cleanup_network()


if __name__ == "__main__":
    main()
