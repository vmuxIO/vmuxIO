from dataclasses import dataclass, field
from enum import Enum
from logging import error, info, debug
from time import sleep
from os.path import isfile, join as path_join
from copy import deepcopy

from server import Server, Host, Guest, LoadGen


class Machine(Enum):
    # Machine types

    # host machine
    HOST = "host"

    # VM with machine type PC
    PCVM = "pcvm"

    # VM with machine type MicroVM
    MICROVM = "microvm"


class Interface(Enum):
    # Interface types

    # Physical NIC (only works with host machine type)
    PNIC = "pnic"

    # Bridge to physical NIC on host, and for VM additionally VirtIO NIC
    # connected to it via TAP device
    BRIDGE = "bridge"

    # MacVTap to physical NIC on host, and for VM additionally VirtIO NIC
    # connected to it
    MACVTAP = "macvtap"

    # VFIO-passthrough to physical NIC for the VM
    VFIO = "vfio"

    # VMux-passthrough to physical NIC for the VM
    VMUX = "vmux"


class Reflector(Enum):
    # Reflector types

    # MoonGen reflector
    MOONGEN = "moongen"

    # XDP reflector
    XDP = "xdp"


@dataclass
class LoadLatencyTest(object):
    """
    Load latency test class
    """
    machine: Machine
    interface: Interface
    mac: str
    qemu: str
    vhost: bool
    ioregionfd: bool
    reflector: Reflector
    rate: int
    size: int
    runtime: int
    repetitions: int
    warmup: bool
    cooldown: bool
    outputdir: str

    def test_infix(self):
        if self.machine == Machine.HOST:
            return (
                f"{self.machine.value}_{self.interface.value}" +
                f"_{self.reflector.value}_{self.rate}kpps_{self.size}B" +
                f"_{self.runtime}s"
            )
        else:
            return (
                f"{self.machine.value}_{self.interface.value}" +
                f"_{self.qemu}_vhost{'on' if self.vhost else 'off'}" +
                f"_ioregionfd{'on' if self.ioregionfd else 'off'}" +
                f"_{self.reflector.value}_{self.rate}kpps_{self.size}B" +
                f"_{self.runtime}s"
            )

    def output_filepath(self, repetition: int):
        return path_join(
            self.outputdir,
            f"output_{self.test_infix()}_rep{repetition}.log"
        )

    def histogram_filepath(self, repetition: int):
        return path_join(
            self.outputdir,
            f"histogram_{self.test_infix()}_rep{repetition}.csv"
        )

    def test_done(self, repetition: int):
        output_file = self.output_filepath(repetition)
        histogram_file = self.histogram_filepath(repetition)

        return isfile(output_file) and isfile(histogram_file)

    def needed(self):
        for repetition in range(self.repetitions):
            if not self.test_done(repetition):
                return True
        return False

    def __str__(self):
        return ("LoadLatencyTest(" +
                f"machine={self.machine.value}, " +
                f"interface={self.interface.value}, " +
                f"mac={self.mac}, " +
                f"qemu={self.qemu}, " +
                f"vhost={self.vhost}, " +
                f"ioregionfd={self.ioregionfd}, " +
                f"reflector={self.reflector.value}, " +
                f"rate={self.rate}, " +
                f"size={self.size}, " +
                f"runtime={self.runtime}, " +
                f"repetitions={self.repetitions}, " +
                f"outputdir={self.outputdir})")

    def run(self, loadgen: LoadGen):
        info(f"Running test {self}")

        remote_output_file = "/tmp/output.log"
        remote_histogram_file = '/tmp/histogram.csv'

        if self.warmup:
            # warm-up
            sleep(10)
            try:
                loadgen.run_l2_load_latency(self.mac, 0, 20,
                                            histfile=remote_histogram_file,
                                            outfile=remote_output_file)
            except Exception as e:
                error(f'Failed to run warm-up due to exception: {e}')
            sleep(25)
            loadgen.stop_l2_load_latency()

        for repetition in range(self.repetitions):
            if self.test_done(repetition):
                debug(f"Skipping repetition {repetition}, already done")
                continue
            debug(f'Running repetition {repetition}')

            if self.cooldown:
                # cool-down
                sleep(20)

            try:
                loadgen.exec(f'sudo rm -f {remote_output_file} ' +
                             f'{remote_histogram_file}')
                loadgen.run_l2_load_latency(self.mac, self.rate,
                                            self.runtime, self.size,
                                            histfile=remote_histogram_file,
                                            outfile=remote_output_file)
            except Exception as e:
                error(f'Failed to run test due to exception: {e}')
                continue

            sleep(self.runtime + 5)
            try:
                loadgen.wait_for_success(f'ls {remote_histogram_file}')
            except TimeoutError:
                error('Waiting for histogram file to appear timed out')
                continue
            sleep(1)
            # TODO here a tmux_exists function would come in handy

            # TODO stopping still fails when the tmux session
            # does not exist
            # loadgen.stop_l2_load_latency()

            # download results
            loadgen.copy_from(remote_output_file,
                              self.output_filepath(repetition))
            loadgen.copy_from(remote_histogram_file,
                              self.histogram_filepath(repetition))

    def accumulate(self, force: bool = False):
        assert self.repetitions > 0, 'Reps must be greater than 0.'
        if self.repetitions == 1:
            debug('Skipping accumulation, there is only one repetition.')
            return

        acc_hist_filename = f'acc_histogram_{self.test_infix()}.csv'
        acc_hist_filepath = path_join(self.outputdir, acc_hist_filename)
        if not force and isfile(acc_hist_filepath):
            debug('Skipping accumulation, already done.')
            return

        info(f"Accumulating histograms for {self}")
        histogram = {}
        for repetition in range(self.repetitions):
            assert self.test_done(repetition), 'Test not done yet'

            with open(self.histogram_filepath(repetition), 'r') as f:
                for line in f:
                    if line.startswith('#'):
                        continue
                    key, value = [int(n) for n in line.split(',')]
                    if key not in histogram:
                        histogram[key] = 0
                    histogram[key] += value

        with open(acc_hist_filepath, 'w') as f:
            for key, value in histogram.items():
                f.write(f'{key},{value}\n')


@dataclass
class LoadLatencyTestGenerator(object):
    """
    Load latency test generator class
    """
    machines: set[Machine]
    interfaces: set[Interface]
    qemus: set[str]
    vhosts: set[bool]
    ioregionfds: set[bool]
    reflectors: set[Reflector]
    rates: set[int]
    sizes: set[int]
    runtimes: set[int]
    repetitions: int
    warmup: bool
    cooldown: bool
    accumulate: bool
    outputdir: str

    full_test_tree: dict = field(init=False, repr=False, default=None)
    todo_test_tree: dict = field(init=False, repr=False, default=None)

    def __post_init__(self):
        info('Initializing test generator:')
        info(f'  machines   : {set(m.value for m in self.machines)}')
        info(f'  interfaces : {set(i.value for i in self.interfaces)}')
        info(f'  qemus      : {self.qemus}')
        info(f'  vhosts     : {self.vhosts}')
        info(f'  ioregionfds: {self.ioregionfds}')
        info(f'  reflectors : {set(r.value for r in self.reflectors)}')
        info(f'  rates      : {self.rates}')
        info(f'  size       : {self.sizes}')
        info(f'  runtimes   : {self.runtimes}')
        info(f'  repetitions: {self.repetitions}')
        info(f'  warmup     : {self.warmup}')
        info(f'  cooldown   : {self.cooldown}')
        info(f'  accumulate : {self.accumulate}')
        info(f'  outputdir  : {self.outputdir}')

    def generate(self, host: Host):
        self.full_test_tree = self.create_test_tree(host)
        self.todo_test_tree = self.create_needed_test_tree(self.full_test_tree)

    def setup_interface(self, host: Host, machine: Machine,
                        interface: Interface, bridge_mac: str = None):
        if machine != Machine.HOST:
            host.setup_admin_bridge()
            host.setup_admin_tap()
            host.modprobe_test_iface_drivers()
        if interface == Interface.BRIDGE:
            if machine == Machine.HOST:
                host.setup_test_bridge()
            else:
                host.setup_test_br_tap()
        elif interface == Interface.MACVTAP:
            host.setup_test_macvtap()
        elif interface in [Interface.VFIO, Interface.VMUX]:
            host.delete_nic_ip_addresses(host.test_iface)
            host.bind_device(host.test_iface_addr, host.test_iface_vfio_driv)

    def start_reflector(self, server: Server, reflector: Reflector,
                        iface: str = None):
        if reflector == Reflector.MOONGEN:
            server.bind_test_iface()
            server.setup_hugetlbfs()
            server.start_moongen_reflector()
        else:
            server.start_xdp_reflector(iface)
        sleep(5)

    def stop_reflector(self, server: Server, reflector: Reflector,
                       interface: Interface, iface: str = None):
        if reflector == Reflector.MOONGEN:
            server.stop_moongen_reflector()
            if interface in [Interface.VFIO, Interface.VMUX]:
                server.unbind_device(server.test_iface_addr)
                # TODO: The hardcoded driver is a dirty workaround,
                #       we need to fix this later on
                server.bind_device(server.test_iface_addr, 'ice')
            else:
                server.release_test_iface()
        else:
            server.stop_xdp_reflector(iface)

    def run_guest(self, host: Host, machine: Machine,
                  interface: Interface, qemu: str, vhost: bool,
                  ioregionfd: bool):
        net_type = None
        if interface == Interface.BRIDGE:
            net_type = 'brtap'
        elif interface == Interface.MACVTAP:
            net_type = 'macvtap'
        elif interface == Interface.VFIO:
            net_type = 'vfio'
        elif interface == Interface.VMUX:
            net_type = 'vmux'
        host.run_guest(
            net_type=net_type,
            machine_type='pc' if machine == Machine.PCVM else 'microvm',
            root_disk=None,
            debug_qemu=False,
            ioregionfd=ioregionfd,
            qemu_build_dir=qemu,
            vhost=vhost
        )

    def create_interface_test_tree(self, machine: Machine,
                                   interface: Interface, mac: str, qemu: str,
                                   vhost: bool, ioregionfd: bool,
                                   reflector: Reflector):
        tree = {}
        for rate in self.rates:
            tree[rate] = {}
            for size in self.sizes:
                tree[rate][size] = {}
                for runtime in self.runtimes:
                    test = LoadLatencyTest(
                        machine=machine,
                        interface=interface,
                        mac=mac,
                        qemu=qemu,
                        vhost=vhost,
                        ioregionfd=ioregionfd,
                        reflector=reflector,
                        rate=rate,
                        size=size,
                        runtime=runtime,
                        repetitions=self.repetitions,
                        warmup=self.warmup,
                        cooldown=self.cooldown,
                        outputdir=self.outputdir,
                    )
                    tree[rate][size][runtime] = test
        return tree

    def create_test_tree(self, host: Host):
        tree = {}
        count = 0
        interface_test_count = len(self.rates) * len(self.runtimes)
        # host part
        mac = host.test_iface_mac
        if Machine.HOST in self.machines:
            m = Machine.HOST
            q = None
            v = None
            io = None
            tree[m] = {}
            for i in self.interfaces:
                tree[m][i] = {}
                tree[m][i][q] = {}
                tree[m][i][q][v] = {}
                tree[m][i][q][v][io] = {}
                for r in self.reflectors:
                    if (i != Interface.PNIC and
                            r == Reflector.MOONGEN):
                        continue
                    tree[m][i][q][v][io][r] = \
                        self.create_interface_test_tree(
                            machine=m,
                            interface=i,
                            mac=mac,
                            qemu=q,
                            vhost=v,
                            ioregionfd=io,
                            reflector=r
                        )
                    count += interface_test_count
        # vm part
        for m in self.machines - {Machine.HOST}:
            tree[m] = {}
            for i in self.interfaces - {Interface.PNIC}:
                if (m == Machine.MICROVM
                        and i in [Interface.VFIO, Interface.VMUX]):
                    continue
                tree[m][i] = {}
                mac = host.test_iface_mac \
                    if i in [Interface.VFIO, Interface.VMUX] \
                    else host.guest_test_iface_mac
                for q in self.qemus:
                    qemu, _ = q.split(':')
                    tree[m][i][q] = {}
                    for v in self.vhosts:
                        if (v and i in [Interface.VFIO, Interface.VMUX]):
                            continue
                        tree[m][i][q][v] = {}
                        # for io in reversed(list(self.ioregionfds)):
                        for io in self.ioregionfds:
                            if io and (m != Machine.MICROVM or
                                       i in [Interface.VFIO, Interface.VMUX]):
                                continue
                            tree[m][i][q][v][io] = {}
                            for r in self.reflectors:
                                if (m == Machine.MICROVM and
                                        r == Reflector.MOONGEN):
                                    continue
                                tree[m][i][q][v][io][r] = \
                                    self.create_interface_test_tree(
                                        machine=m,
                                        interface=i,
                                        mac=mac,
                                        qemu=qemu,
                                        vhost=v,
                                        ioregionfd=io,
                                        reflector=r
                                    )
                                count += interface_test_count
        info(f'Generated {count} tests')
        return tree

    def create_needed_test_tree(self, test_tree: dict):
        info('Remove already done tests')
        needed = deepcopy(test_tree)
        count = 0
        for m, mtree in test_tree.items():
            for i, itree in mtree.items():
                for q, qtree in itree.items():
                    for v, vtree in qtree.items():
                        for f, ftree in vtree.items():
                            for r, rtree in ftree.items():
                                for a, atree in rtree.items():
                                    for s, stree in atree.items():
                                        for t, test in stree.items():
                                            if not test.needed():
                                                del(needed[m][i][q][v][f][r][a][s][t])
                                            else:
                                                count += 1
                                        if not needed[m][i][q][v][f][r][a][s]:
                                            del(needed[m][i][q][v][f][r][a][s])
                                    if not needed[m][i][q][v][f][r][a]:
                                        del(needed[m][i][q][v][f][r][a])
                                if not needed[m][i][q][v][f][r]:
                                    del(needed[m][i][q][v][f][r])
                            if not needed[m][i][q][v][f]:
                                del(needed[m][i][q][v][f])
                        if not needed[m][i][q][v]:
                            del(needed[m][i][q][v])
                    if not needed[m][i][q]:
                        del(needed[m][i][q])
                if not needed[m][i]:
                    del(needed[m][i])
            if not needed[m]:
                del(needed[m])
        info(f'{count} tests are not done yet')
        return needed

    def run(self, host: Host, guest: Guest, loadgen: LoadGen):
        """
        Run the tests
        """
        # TODO Qemus should contain strings like
        #   normal:/home/networkadmin/qemu-build
        #   replace-ioeventfd:/home/networkadmin/qemu-build-2
        # Empty path is also possible.
        # Before the : is the name, this goes to the test runner.
        # The rest is the path to the qemu build directory and just used here
        # to start the guest.
        # In case no name is given, we could number them.
        if not self.todo_test_tree:
            info('No tests to run')
            return

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

        for machine, mtree in self.todo_test_tree.items():
            info(f"Running {machine.value} tests")

            for interface, itree in mtree.items():
                debug(f"Setting up interface {interface.value}")
                self.setup_interface(host, machine, interface)

                for qemu, qtree in itree.items():
                    qemu_name = None
                    qemu_path = None
                    if qemu:
                        qemu_name, qemu_path = qemu.split(':')
                    # TODO make sure the qemu_path exists and qemu is
                    # executable

                    for vhost, vtree in qtree.items():
                        for ioregionfd, ftree in vtree.items():

                            if machine == Machine.HOST:
                                dut = host
                            else:
                                dut = guest
                                debug(f"Running guest {machine.value} " +
                                      f"{interface.value} {qemu_name} " +
                                      f"{vhost} {ioregionfd}")
                                if interface == Interface.VMUX:
                                    host.start_vmux()
                                self.run_guest(host, machine, interface,
                                               qemu_path, vhost, ioregionfd)
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

                            for reflector, rtree in ftree.items():
                                debug(f"Starting reflector {reflector.value}")
                                self.start_reflector(dut, reflector)

                                for rate, atree in rtree.items():
                                    for size, stree in atree.items():
                                        for runtime, test in stree.items():
                                            test.run(loadgen)
                                            if self.accumulate:
                                                # TODO we probably need to put
                                                # this somewhere else to
                                                # make sure it runs even if the
                                                # tests are already done
                                                test.accumulate()

                                debug(f"Stopping reflector {reflector.value}")
                                self.stop_reflector(dut, reflector, interface)

                            if machine != Machine.HOST:
                                debug(f"Killing guest {machine.value} " +
                                      f"{interface.value} {qemu_name} " +
                                      f"{vhost} {ioregionfd}")
                                host.kill_guest()
                                if interface == Interface.VMUX:
                                    host.stop_vmux()

                debug(f"Tearing down interface {interface.value}")
                host.cleanup_network()

    def force_accumulate(self):
        """
        Force accumulation of all tests
        """
        for machine, mtree in self.full_test_tree.items():
            for interface, itree in mtree.items():
                for qemu, qtree in itree.items():
                    for vhost, vtree in qtree.items():
                        for ioregionfd, ftree in vtree.items():
                            for reflector, rtree in ftree.items():
                                for rate, atree in rtree.items():
                                    for runtime, test in atree.items():
                                        if self.accumulate:
                                            test.accumulate(force=True)


# if __name__ == "__main__":
#     machines = {Machine.HOST, Machine.PCVM, Machine.MICROVM}
#     interfaces = {Interface.PNIC, Interface.BRIDGE, Interface.MACVTAP,
#                   Interface.VFIO}
#     qemus = {"/home/networkadmin/qemu-build/qemu-system-x86_64",
#              "/home/networkadmin/qemu-build2/qemu-system-x86_64"}
#     vhosts = {True, False}
#     ioregionfds = {True, False}
#     reflectors = {Reflector.MOONGEN, Reflector.XDP}
#     rates = {1, 10, 100}
#     runtimes = {30, 60}
#     repetitions = 3
#     warmup = False
#     cooldown = False
#     accumulate = True
#     outputdir = "/home/networkadmin/loadlatency"
#
#     generator = LoadLatencyTestGenerator(
#         machines, interfaces, qemus, vhosts, ioregionfds, reflectors,
#         rates, runtimes, repetitions, warmup, cooldown, accumulate, outputdir
#     )
