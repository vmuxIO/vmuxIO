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
from util import safe_cast
from typing import Iterator, cast
import time
import yaml
from root import *

OUT_DIR = "/tmp/out1"

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


    # interface = Interface.VMUX_PT
    # # TODO put early skip check here
    # with measurement.virtual_machine(interface) as guest:
    #
    #     # guest: set up interfaces and networking
    #
    #     debug("Detecting guest test interface")
    #     guest.detect_test_iface()
    #     guest.bind_test_iface()
    #     guest.setup_hugetlbfs()
    #
    #     # the actual test
    #
    #     loadgen.stop_moongen_reflector()
    #     loadgen.start_moongen_reflector()
    #
    #     test = LoadLatencyTest(
    #         machine=Machine.PCVM,
    #         interface=interface,
    #         mac=loadgen.test_iface_mac,
    #         qemu="measure-vnf", # abuse this as comment field
    #         vhost=False,
    #         ioregionfd=False,
    #         reflector=Reflector.MOONGEN,
    #         rate=1,
    #         size=64,
    #         runtime=30,
    #         repetitions=1,
    #         warmup=False,
    #         cooldown=False,
    #         outputdir=OUT_DIR,
    #     )
    #     guest_: LoadGen = cast(LoadGen, guest) # trust me bro, this works
    #     test.run(guest_)
    #
    #     loadgen.stop_moongen_reflector()

    # create per-VM docker-compose
    with open(f"{PROJECT_ROOT}/subprojects/deathstarbench/hotelReservation/docker-compose.yml", "r") as file:
        docker_compose_full = yaml.safe_load(file)
    version = docker_compose_full["version"]
    docker_compose = dict()
    docker_compose["version"] = f"version"
    docker_compose["services"] = dict()
    docker_compose["services"]["consul"] = docker_compose_full["services"]["consul"]
    docker_compose["services"]["consul"]["network_mode"] = "host" # --network host
    docker_compose_str = yaml.dump(docker_compose)
    docker_image = docker_compose["services"]["consul"]["image"]

    docker_images = f"{measurement.config['guest']['moonprogs_dir']}/../../VMs/docker-images.tar"

    interface = Interface.BRIDGE

    with measurement.virtual_machine(interface) as guest:
        try:
            # guest: set up interfaces and networking

            # debug("Detecting guest test interface")
            # interface is already bound and up (without ip)


            # the actual test

            guest.exec(f'echo "{docker_compose_str}" > docker-compose.yaml')
            guest.exec(f"docker load -i {docker_images}")
            guest.exec(f"docker-compose up -d")

            # TODO start more services and so on

            breakpoint()
            guest.exec(f"docker-compose down")
            breakpoint()

            loadgen.stop_moongen_reflector()
            loadgen.start_moongen_reflector()

            # TODO check if we can skip
            time.sleep(1) # give ip link time to come up
            info("Running kni-latency")
            remote_output_file = "/tmp/out.log"
            guest.exec(
                    f"{measurement.config['guest']['moonprogs_dir']}/../kni-latency/kni-latency "
                    f"{loadgen.test_iface_mac} {guest.test_iface} "
                    f"| tee {remote_output_file}"
                    )
            guest.copy_from(remote_output_file,
                        f"{OUT_DIR}/measure_vnf_rep0.log")

            loadgen.stop_moongen_reflector()
        except Exception:
            breakpoint()


if __name__ == "__main__":
    main()
