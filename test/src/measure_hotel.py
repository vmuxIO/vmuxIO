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
from typing import Iterator, cast, List, Dict
import time
import yaml
from root import *

OUT_DIR = "/tmp/out1"

class DeathStarBench:

    docker_compose: List[str] = [] # service name -> strings of content of docker-compose.yamls    

    def parse_docker_compose(self):
        # create per-VM docker-compose
        with open(f"{PROJECT_ROOT}/subprojects/deathstarbench/hotelReservation/docker-compose.yml", "r") as file:
            docker_compose_full = yaml.safe_load(file)
        version = docker_compose_full["version"]
        for service in docker_compose_full["services"].keys():
            docker_compose = dict()
            docker_compose["version"] = f"{version}"
            docker_compose["services"] = dict()
            docker_compose["services"][service] = docker_compose_full["services"]["consul"]
            docker_compose["services"][service]["network_mode"] = "host" # --network host
            self.docker_compose += [yaml.dump(docker_compose)]

def main() -> None:
    # general measure init
    measurement = Measurement()
    host, loadgen = measurement.hosts()

    # loadgen: set up interfaces and networking

    # debug('Binding loadgen interface')
    # try:
    #     loadgen.delete_nic_ip_addresses(loadgen.test_iface)
    # except Exception:
    #     pass
    # loadgen.bind_test_iface()
    # loadgen.setup_hugetlbfs()

    deathstar = DeathStarBench()
    deathstar.parse_docker_compose()
    num_vms = len(deathstar.docker_compose)

    docker_images = f"{measurement.config['guest']['moonprogs_dir']}/../../VMs/docker-images.tar"

    interface = Interface.BRIDGE

    breakpoint()
    with measurement.virtual_machines(interface, num=800) as guests:
        breakpoint()
        try:
            for i in range(0, num_vms):
                guests[i].exec(f'echo "{deathstar.docker_compose[i]}" > docker-compose.yaml')
                guests[i].exec(f"docker load -i {docker_images}")
                guests[i].exec(f"docker-compose up -d")

            # guest: set up interfaces and networking

            # debug("Detecting guest test interface")
            # interface is already bound and up (without ip)


            # the actual test


            # TODO start more services and so on

            breakpoint()
            for i in range(0, num_vms):
                guests[i].exec(f"docker-compose down")
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
