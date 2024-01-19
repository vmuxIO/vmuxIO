import autotest as autotest
from configparser import ConfigParser
from argparse import (ArgumentParser, ArgumentDefaultsHelpFormatter, Namespace,
                      FileType, ArgumentTypeError)
from argcomplete import autocomplete
from logging import (info, debug, error, warning,
                     DEBUG, INFO, WARN, ERROR)
from server import Host, Guest, LoadGen
from loadlatency import Interface, Machine, LoadLatencyTest, Reflector
from measure import Measurement


def main() -> None:
    # general measure init
    measurement = Measurement()
    host, loadgen = measurement.hosts()

    # loadgen: set up interfaces and networking

    debug('Binding loadgen interface')
    try:
        loadgen.delete_nic_ip_addresses(loadgen.test_iface)
    except Exception:
        pass
    loadgen.bind_test_iface()
    loadgen.setup_hugetlbfs()


    interface = Interface.VMUX_PT

    with measurement.virtual_machine(interface) as guest:

        # guest: set up interfaces and networking

        debug("Detecting guest test interface")
        guest.detect_test_iface()

        breakpoint()
        info("heureka")
        guest.bind_test_iface()
        guest.setup_hugetlbfs()

        # the actual test

        loadgen.start_moongen_reflector()

        test = LoadLatencyTest(
            machine=Machine.PCVM,
            interface=Interface.VMUX_PT,
            mac=loadgen.test_iface_mac,
            qemu="measure-vnf", # abuse this as comment field
            vhost=False,
            ioregionfd=False,
            reflector=Reflector.MOONGEN,
            rate=1,
            size=64,
            runtime=30,
            repetitions=1,
            warmup=False,
            cooldown=False,
            outputdir="/tmp/out1",
        )
        guest: LoadGen = guest # trust me bro, this works
        test.run(guest)
        breakpoint()

        loadgen.stop_moongen_reflector()


if __name__ == "__main__":
    main()
