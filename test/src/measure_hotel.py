import autotest as autotest
from configparser import ConfigParser
from argparse import (ArgumentParser, ArgumentDefaultsHelpFormatter, Namespace,
                      FileType, ArgumentTypeError)
from argcomplete import autocomplete
from logging import (info, debug, error, warning,
                     DEBUG, INFO, WARN, ERROR)
from server import Host, Guest, LoadGen, MultiHost
from loadlatency import Interface, Machine, LoadLatencyTest, Reflector
from measure import Measurement, end_foreach
from util import safe_cast
from typing import Iterator, cast, List, Dict, Callable, Tuple
import time
import yaml
import json
from root import *

OUT_DIR = "/tmp/out1"

class DeathStarBench:

    config = ""
    docker_compose: Dict[int, str] = {} # vm id -> strings of content of docker-compose.yamls    
    service_map: Dict[int, str] = {} # vm id -> service name

    def parse_docker_compose(self):
        # create per-VM docker-compose
        with open(f"{PROJECT_ROOT}/subprojects/deathstarbench/hotelReservation/docker-compose.yml", "r") as file:
            docker_compose_full = yaml.safe_load(file)
        version = docker_compose_full["version"]
        vm_number = 1
        for service in docker_compose_full["services"].keys():
            docker_compose = dict()
            # docker_compose["version"] = f'{version} '
            docker_compose["services"] = dict()
            docker_compose["services"][service] = docker_compose_full["services"][service]
            docker_compose["services"][service]["network_mode"] = "host" # --network host
            docker_compose["services"][service]["depends_on"] = dict() # ignore stated dependencies (cant be resolved because they are not local)
            docker_compose["services"][service]["volumes"] = [ "./config.json:/config.json" ]

            self.docker_compose[vm_number] = str(yaml.dump(docker_compose))
            self.service_map[vm_number] = service
            vm_number += 1
        # breakpoint()
        # print("fo")

    def parse_config(self):
        with open(f"{PROJECT_ROOT}/subprojects/deathstarbench/hotelReservation/config.json", "r") as file:
            config = json.load(file)

        for key, maybeServiceName in config.items():
            matched_service = list(filter(lambda item: item[1] == maybeServiceName.split(":")[0], self.service_map.items()))
            assert len(matched_service) < 2 # we expect 1 or none match
            if len(matched_service) == 0: continue # no match, nothing to replace
            matched_service = matched_service[0]
            service_name = matched_service[1]
            vm_number = matched_service[0]
            ip = MultiHost.ip("192.168.56.20", vm_number)
            config[key] = maybeServiceName.replace(service_name, ip)
        self.config = str(json.dumps(config))


def main() -> None:
    # general measure init
    measurement = Measurement()
    host, loadgen = measurement.hosts()

    deathstar = DeathStarBench()
    deathstar.parse_docker_compose()
    deathstar.parse_config()
    num_vms = len(deathstar.docker_compose)

    docker_images = f"{measurement.config['guest']['moonprogs_dir']}/../../VMs/docker-images.tar"

    interface = Interface.BRIDGE

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

            def foreach(i, guest): # pyright: ignore[reportGeneralTypeIssues]
                guest.setup_test_iface_ip_net()
                
                guest.write(deathstar.docker_compose[i], "docker-compose.yaml")
                guest.write(deathstar.config, "config.json")
                guest.exec(f"docker load -i {docker_images}")
                guest.exec(f"docker-compose up -d")
            end_foreach(guests, foreach)

            # the actual test

            info("Running wrk2")

            remote_output_file = "/tmp/output.log"
            duration = 11
            frontend_vm_number = list(filter(lambda i: i[1] == "frontend", deathstar.service_map.items()))[0][0]
            frontend_ip = MultiHost.ip("10.1.0.20", frontend_vm_number)
            loadgen.exec(f"sudo rm {remote_output_file} || true")
            LoadGen.stop_wrk2(loadgen)
            LoadGen.start_wrk2(loadgen, frontend_ip, duration=duration, outfile=remote_output_file)
            time.sleep(duration + 1)
            try:
                loadgen.wait_for_success(f'[[ $(tail -n 1 {remote_output_file}) = *"AUTOTEST_DONE"* ]]')
            except TimeoutError:
                error('Waiting for output file to appear timed out')
            loadgen.copy_from(remote_output_file,
                        f"{OUT_DIR}/measure_hotel_rep0.log")

            LoadGen.stop_wrk2(loadgen)

            # the actual test end

            def foreach(i, guest): # pyright: ignore[reportGeneralTypeIssues]
                guest.exec(f"docker-compose down")
            end_foreach(guests, foreach)

        except Exception as e:
            print(f"foo {e}")
            # breakpoint()
            print("...")


if __name__ == "__main__":
    main()
