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
from os.path import isfile, join as path_join
import yaml
import json
from root import *
from dataclasses import dataclass, field

OUT_DIR: str
BRIEF: bool

@dataclass
class DeathStarBenchTest:
    repetitions: int
    app: str # e.g. hotelReservation
    rps: int # requests per second
    interface: str # network interface used
    num_vms: int

    def test_infix(self):
        return f"{self.app}_{self.interface}_{self.num_vms}vms_{self.rps}rps"

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


class DeathStarBench:

    app: str
    docker_image_tar: str
    docker_compose: Dict[int, str] = {} # vm id -> strings of content of docker-compose.yamls    
    service_map: Dict[int, str] = {} # vm id -> service name
    extra_hosts: str = "" # /etc/hosts format
    frontend_url: str = "host:port"

    def __init__(self, app: str, moonprogs_dir: str):
        assert app in [ 
            "hotelReservation",
            "socialNetwork",
            # "mediaMicroservices",
        ] # others are not implemented

        self.app = app
        self.docker_image_tar = f"{moonprogs_dir}/../../VMs/docker-images-{self.app}.tar"

    def parse_docker_compose(self):
        # create per-VM docker-compose
        with open(f"{PROJECT_ROOT}/subprojects/deathstarbench/{self.app}/docker-compose.yml", "r") as file:
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
            if self.app == "hotelReservation":
                docker_compose["services"][service]["volumes"] = [ "./config.json:/config.json" ]

            self.docker_compose[vm_number] = str(yaml.dump(docker_compose))
            self.service_map[vm_number] = service
            self.extra_hosts += f"{MultiHost.ip('192.168.56.20', vm_number)} {service}\n"
            vm_number += 1
        # breakpoint()
        # print("fo")

    def parse_hotel_config(self) -> str:
        """
        return string containing config json
        """
        assert self.app == "hotelReservation"
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
        return str(json.dumps(config))


    def install_social_configs(self, guest: Guest):
        assert self.app == "socialNetwork"

        guest.update_extra_hosts(self.extra_hosts)
        guest.exec("rm -r ./* || true")
        local_dir = f"{PROJECT_ROOT}/subprojects/deathstarbench/{self.app}/*"
        guest.copy_to(local_dir, "./", recursive=True)

    def vm_number_of(self, service_name: str) -> int:
        try:
            ret = list(filter(lambda i: i[1] == service_name, self.service_map.items()))[0][0]
            return ret
        except IndexError:
            raise Exception(f"DeathStarBench.vm_number_of: service {service_name} not found")


    def run(self, host: Host, loadgen: LoadGen, guests: Dict[int, Guest], test: DeathStarBenchTest):
        assert test.app == self.app

        def foreach_parallel(i, guest): # pyright: ignore[reportGeneralTypeIssues]
            if self.app == "hotelReservation":
                guest.write(self.parse_hotel_config(), "config.json")
                frontend_ip = MultiHost.ip("10.1.0.20", self.vm_number_of("frontend"))
                self.frontend_url = f"{frontend_ip}:5000"
            if self.app == "socialNetwork":
                self.install_social_configs(guest)
                frontend_ip = MultiHost.ip("10.1.0.20", self.vm_number_of("nginx-thrift"))
                self.frontend_url = f"{frontend_ip}:8080"

            guest.write(self.docker_compose[i], "docker-compose.yml")
            guest.exec(f"docker load -i {self.docker_image_tar}")
        end_foreach(guests, foreach_parallel)

        def foreach_parallel(i, guest): # pyright: ignore[reportGeneralTypeIssues]
            guest.exec(f"docker-compose up -d")
        end_foreach(guests, foreach_parallel)

        info(f"Running wrk2 {test.repetitions} times")

        remote_output_file = "/tmp/output.log"
        duration_s = 61 if not BRIEF else 11
        for repetition in range(test.repetitions):
            local_output_file = test.output_filepath(repetition)
            loadgen.exec(f"sudo rm {remote_output_file} || true")
            LoadGen.stop_wrk2(loadgen)
            script = ""
            if self.app == "hotelReservation":
                script = "./wrk2/scripts/hotel-reservation/mixed-workload_type_1.lua"
            if self.app == "socialNetwork":
                script = "./wrk2/scripts/social-network/mixed-workload.lua"
            LoadGen.start_wrk2(loadgen, self.frontend_url, script, duration=duration_s, outfile=remote_output_file)
            time.sleep(duration_s + 1)
            try:
                loadgen.wait_for_success(f'[[ $(tail -n 1 {remote_output_file}) = *"AUTOTEST_DONE"* ]]')
            except TimeoutError:
                error('Waiting for output file to appear timed out')
            loadgen.copy_from(remote_output_file, local_output_file)

            LoadGen.stop_wrk2(loadgen)

        # the actual test end

        def foreach_parallel(i, guest): # pyright: ignore[reportGeneralTypeIssues]
            guest.exec(f"docker-compose down")
        end_foreach(guests, foreach_parallel)


def main() -> None:
    # general measure init
    measurement = Measurement()
    host, loadgen = measurement.hosts()
    from measure import OUT_DIR as M_OUT_DIR, BRIEF as M_BRIEF
    global OUT_DIR
    global BRIEF
    OUT_DIR = M_OUT_DIR
    BRIEF = M_BRIEF

    moonprogs_dir = f"{measurement.config['guest']['moonprogs_dir']}"
    # app = "hotelReservation"
    app = "socialNetwork"
    deathstar = DeathStarBench(app, moonprogs_dir)
    deathstar.parse_docker_compose()
    num_vms = len(deathstar.docker_compose)

    interfaces = [ Interface.VMUX_EMU, Interface.BRIDGE_E1000, Interface.BRIDGE ]
    rpsList = [ 10, 100 ]
    if BRIEF:
        interfaces = [ Interface.BRIDGE_E1000 ]
        rpsList = [ 10 ]

    for interface in interfaces:
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

                def foreach_parallel(i, guest): # pyright: ignore[reportGeneralTypeIssues]
                    guest.setup_test_iface_ip_net()
                end_foreach(guests, foreach_parallel)
                
                for rps in rpsList:
                    # the actual test
                    test = DeathStarBenchTest(
                            repetitions=3 if not BRIEF else 1,
                            app=app,
                            rps=rps,
                            interface=interface.value,
                            num_vms=num_vms
                            )
                    if test.needed():
                        info(f"running {test}")
                        deathstar.run(host, loadgen, guests, test)
                    else:
                        info(f"skipping {test}")

            except Exception as e:
                print(f"foo {e}")
                breakpoint()
                print("...")

if __name__ == "__main__":
    main()
