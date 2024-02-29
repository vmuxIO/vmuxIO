from dataclasses import dataclass, field
from subprocess import check_output, CalledProcessError, STDOUT
from socket import getfqdn
from logging import debug, warning, error
from time import sleep
from datetime import datetime
from abc import ABC
from os import listdir, remove
from os.path import join as path_join
from os.path import dirname as path_dirname
from os.path import dirname as path_getsize
from typing import Optional
import netaddr
from pathlib import Path
import copy
import ipaddress
import base64

BRIDGE_QUEUES: int = 0; # legacy default: 4
MAX_VMS: int = 35; # maximum number of VMs expected (usually for cleanup functions that dont know what to clean up)

class MultiHost:
    """
    Starts from vm_number 1. Vm_number 0 leads to legacy outputs without numbers. -1 returns string matching all numbers.
    """
    @staticmethod
    def range(num_vms: int) -> range:
        return range(1, num_vms + 1)

    @staticmethod
    def ssh_hostname(ssh_hostname: str, vm_number: int):
        if vm_number == 0: return ssh_hostname
        fqdn = ssh_hostname.split(".")
        fqdn[0] = f"{fqdn[0]}{vm_number}"
        return ".".join(fqdn)

    @staticmethod
    def mac(base_mac: str, vm_number: int) -> str:
        if vm_number == 0: return base_mac
        base = netaddr.EUI(base_mac)
        value = base.value + vm_number
        fmt = netaddr.mac_unix
        fmt.word_fmt = "%.2x"
        return str(netaddr.EUI(value).format(fmt))

    @staticmethod
    def ip(base_ip: str, vm_number: int) -> str:
        """
        Increment ips with or without subnets
        """
        ip = base_ip.split("/") # split subnet mask
        start = ipaddress.IPv4Address(ip[0])
        if len(ip) == 1:
            return f"{start + vm_number - 1}"
        else:
            # re-attach subnet
            return f"{start + vm_number - 1}/{ip[1]}"

    @staticmethod
    def generic_path(path_: str, vm_number: int) -> str:
        if vm_number == 0: return path_
        path = Path(path_)
        path = path.with_name(path.stem + str(vm_number) + path.suffix)
        return str(path)

    @staticmethod
    def disk_path(root_disk_file: str, vm_number: int) -> str:
        return MultiHost.generic_path(root_disk_file, vm_number)

    @staticmethod
    def vfu_path(vfio_user_socket_path: str, vm_number: int) -> str:
        return MultiHost.generic_path(vfio_user_socket_path, vm_number)

    @staticmethod
    def cloud_init(disk_path: str, vm_number: int) -> str:
        init_disk = Path(disk_path).resolve()
        init_disk = init_disk.parent / f"cloud-init/vm{vm_number}.img"
        return str(init_disk)

    @staticmethod
    def iface_name(tap_name: str, vm_number: int):
        """
        kernel interface name
        """
        if vm_number == 0: return tap_name
        max_len = 15
        length = max_len - len("-999")
        if vm_number == -1: return f"{tap_name[:length]}-"
        return f"{tap_name[:length]}-{vm_number}"


    @staticmethod
    def enumerate(enumeratable: str, vm_number: int) -> str:
        if vm_number == 0: return enumeratable
        return f"{enumeratable}-vm{vm_number}"


@dataclass
class Server(ABC):
    """
    Server class.

    This class represents a server.

    Attributes
    ----------
    fqdn : str
        The fully qualified domain name of the server.
    localhost : bool
        True if the server is localhost.
    nixos : bool
        True if the server is running NixOS.
    test_iface : str
        The name of the interface to test.
    test_iface_addr : str
        The PCI bus address of the interface to test.
    test_iface_driv : str
        The default driver of the interface to test.
    test_iface_dpdk_driv : str
        The DPDK driver of the interface to test.
    test_iface_mac : str
        The MAC address of the interface to test.
    tmux_socket : str
        The name for the tmux socket.
    moongen_dir : str
        The directory of the MoonGen installation.
    moonprogs_dir : str
        The directory with the MoonGen Lua programs.
    xdp_reflector_dir : str
        The directory of the XDP reflector installation.
    localhost : bool
        True if the server is localhost.

    Methods
    -------
    __init__ : Initialize the object.
    __post_init__ : Post initialization.
    is_reachable : Check if the server is reachable.
    __exec_local : Execute a command on the localhost.
    __exec_ssh : Execute a command on the server over SSH.
    exec : Execute command on the server.
    tmux_new : Start a tmux session on the server.
    tmux_kill : Stop a tmux session on the server.
    tmux_send_keys : Send keys to a tmux session on the server.
    __copy_local : Copy a file to the localhost.
    __scp_to : Copy a file to the server.
    __scp_from : Copy a file from the server.
    copy_to : Copy a file to the server.
    copy_from : Copy a file from the server.

    See Also
    --------

    Examples
    --------
    >>> Server('server.test.de')
    Server(fqdn='server.test.de')
    """
    fqdn: str
    test_iface: str
    test_iface_addr: str
    _test_iface_id: int = field(default=None, init=False)
    test_iface_mac: str
    test_iface_driv: str
    test_iface_dpdk_driv: str
    tmux_socket: str
    moongen_dir: str
    moonprogs_dir: str
    xdp_reflector_dir: str
    localhost: bool = False
    nixos: bool = False
    ssh_config: Optional[str] = None
    ssh_as_root: bool = False

    def __post_init__(self: 'Server') -> None:
        """
        Post initialization.

        This method is called after the object is created.

        Parameters
        ----------

        Returns
        -------

        See Also
        --------
        __init__ : Initialize the object.
        """
        self.nixos = True # all hosts are nixos actually
        return 
        self.localhost = self.fqdn == 'localhost' or self.fqdn == getfqdn()
        try:
            self.nixos = self.isfile('/etc/NIXOS')
        except Exception:
            warning(f'Could not run nixos detection on {self.fqdn}')

    def log_name(self: 'Server') -> str:
        """
        Get the log name.

        Parameters
        ----------

        Returns
        -------
        str
            The log name.
        """
        return f'{self.__class__.__name__.lower()} {self.fqdn}'

    def is_reachable(self: 'Server') -> bool:
        """
        Check if the server is reachable.

        Parameters
        ----------

        Returns
        -------
        bool
            True if the server is reachable.

        See Also
        --------
        exec : Execute command on the server.
        """
        if self.localhost:
            return True
        else:
            try:
                check_output(f'ping -c 1 -W 1 {self.fqdn}', shell=True)
            except CalledProcessError:
                return False
            else:
                return True

    def __cmd_with_package(self: 'Server', package: str) -> str:
        """
        Returns a prefix for a console command. The prefix ensures package to be available to command.
        Command will not run in a shell!
        """
        return f"nix shell --inputs-from {self.moonprogs_dir} nixpkgs#{package} --command"

    def __exec_local(self: 'Server', command: str) -> str:
        """
        Execute a command on the localhost.

        This method is called by the exec method if the server is localhost.

        Parameters
        ----------
        command : str
            The command to execute.

        Returns
        -------
        str
            The output of the command.

        See Also
        --------
        exec : Execute command on the server.
        __exec_ssh : Execute a command on the server over SSH.
        """
        return check_output(command, stderr=STDOUT, shell=True).decode('utf-8')

    def __exec_ssh(self: 'Server', command: str) -> str:
        """
        Execute a command on the server over SSH.

        This method is called by the exec method if the server is not
        localhost.

        Parameters
        ----------
        command : str
            The command to execute.

        Returns
        -------
        str
            The output of the command.

        See Also
        --------
        exec : Execute command on the server.
        __exec_local : Execute a command on the localhost.
        """
        options = ""
        if self.ssh_config is not None:
            options = f" -F {self.ssh_config}"

        sudo = ""
        if self.ssh_as_root == True:
            sudo = "sudo "

        return check_output(f"{sudo}ssh{options} {self.fqdn} '{command}'" 
                            # + " 2>&1 | tee /tmp/foo"
                            , stderr=STDOUT, shell=True).decode('utf-8')

    def exec(self: 'Server', command: str, echo: bool = True) -> str:
        """
        Execute command on the server.

        If the server is not localhost the command is executed over SSH.

        Parameters
        ----------
        command : str
            The command to execute.

        Returns
        -------
        str
            The output of the command.

        See Also
        --------
        __exec_local : Execute a command on the localhost.
        __exec_ssh : Execute a command on the server over SSH.

        Example
        -------
        >>> print(server.exec('ls -l'))
        .bashrc
        """
        if echo: debug(f'Executing command on {self.log_name()}: {command}')
        if self.localhost:
            return self.__exec_local(command)
        else:
            return self.__exec_ssh(command)

    def write(self: 'Server', content: str, path: str, verbose: bool = False) -> None:
        if verbose:
            debug(f'Executing command on {self.log_name()}: Writing to file {path} the following:\n{content}')
        else:
            debug(f'Executing command on {self.log_name()}: Writing to file {path}')
        # encode so we dont have to fuck aroudn with quotes (they get removed somewhere)
        b64 = base64.b64encode(bytes(content, 'utf-8')).decode("utf-8")
        self.exec(f"echo {b64} | base64 -d > {path}", echo=False)

    def whoami(self: 'Server') -> str:
        """
        Get the user name.

        Parameters
        ----------

        Returns
        -------
        str
            The user name.
        """
        return self.exec('whoami').strip()

    def gethome(self: 'Server') -> str:
        """
        Get the home directory.

        Parameters
        ----------

        Returns
        -------
        str
            The home directory.
        """
        return self.exec('echo $HOME').strip()

    def isfile(self: 'Server', path: str) -> bool:
        """
        Check if a file exists.

        Parameters
        ----------
        path : str
            The path to the file.

        Returns
        -------
        bool
            True if the file exists.
        """
        return self.exec(f'test -f {path} && echo true || echo false'
                         ).strip() == 'true'

    def isdir(self: 'Server', path: str) -> bool:
        """
        Check if a directory exists.

        Parameters
        ----------
        path : str
            The path to the directory.

        Returns
        -------
        bool
            True if the directory exists.
        """
        return self.exec(f'test -d {path} && echo true || echo false'
                         ).strip() == 'true'

    def check_cpu_freq(self: 'Server') -> None:
        """
        Throw if cpufreq is wrong
        """
        intel_path = "/sys/devices/system/cpu/intel_pstate/no_turbo"
        amd_path = "/sys/devices/system/cpu/cpufreq/boost" # every other CPU
        if (self.isfile(intel_path)):
            try:
                self.exec(f"[[ $(cat {intel_path}) -eq 1 ]]")
            except CalledProcessError:
                error(f"Please run on {self.fqdn}: echo 1 | sudo tee {intel_path}")
                raise Exception(f"Wrong CPU freqency on {self.fqdn}")
        else:
            try:
                self.exec(f"[[ $(cat {amd_path}) -eq 0 ]]")
            except CalledProcessError:
                error(f"Please run on {self.fqdn}: echo 0 | sudo tee {amd_path}")
                raise Exception(f"Wrong CPU freqency on {self.fqdn}")



    def tmux_new(self: 'Server', session_name: str, command: str) -> None:
        """
        Start a tmux session on the server.

        Parameters
        ----------
        session_name : str
            The name of the session.
        command : str
            The command to execute.

        Returns
        -------

        See Also
        --------
        exec : Execute command on the server.
        tmux_kill : Stop a tmux session on the server.
        tmux_send_keys : Send keys to a tmux session on the server.
        """
        _ = self.exec(f'tmux -L {self.tmux_socket}' +
                      f' new-session -s {session_name} -d "{command}"')

    def tmux_kill(self: 'Server', session_name: str) -> None:
        """
        Stop all tmux sessions matching session_name.

        Parameters
        ----------
        session_name : str
            The name of the session.

        Returns
        -------

        See Also
        --------
        exec : Execute command on the server.
        tmux_new : Start a tmux session on the server.
        tmux_send_keys : Send keys to a tmux session on the server.
        """
        _ = self.exec(f'tmux -L {self.tmux_socket}' +
                      ' list-sessions | cut -d ":" -f 1 ' +
                      f'| grep {session_name} | xargs -I {{}} ' +
                      f'tmux -L {self.tmux_socket} kill-session -t {{}} || true')

    def tmux_send_keys(self: 'Server', session_name: str, keys: str) -> None:
        """
        Send keys to a tmux session on the server.

        Parameters
        ----------
        session_name : str
            The name of the session.
        keys : str
            The keys to execute.

        Returns
        -------

        See Also
        --------
        exec : Execute keys on the server.
        tmux_new : Start a tmux session on the server.
        tmux_kill : Stop a tmux session on the server.
        """
        _ = self.exec(f'tmux -L {self.tmux_socket}' +
                      f' send-keys -t {session_name} {keys}')

    def __copy_local(self: 'Server', source: str, destination: str, recursive: bool = False) -> None:
        """
        Copy a file from the localhost to the server.

        This method is called by the copy method if the server is localhost.

        Parameters
        ----------
        source : str
            The source file.
        destination : str
            The destination file.

        Returns
        -------

        See Also
        --------
        copy : Copy a file from the server to the localhost.
        __copy_ssh : Copy a file from the server to the server over SSH.
        """
        options = ""
        if recursive:
            options += " -r"

        self.__exec_local(f'cp{options} {source} {destination}')

    def __scp_to(self: 'Server', source: str, destination: str, recursive: bool = False) -> None:
        """
        Copy a file from the localhost to the server.

        This method is called by the copy method if the server is not
        localhost.

        Parameters
        ----------
        source : str
            The source file.
        destination : str
            The destination file.

        Returns
        -------

        See Also
        --------
        copy : Copy a file from the server to the localhost.
        __copy_local : Copy a file from the server to the server over SSH.
        __scp_from : Copy a file from the server to the server over SSH.
        """
        options = ""
        if self.ssh_config is not None:
            options = f" -F {self.ssh_config}"

        if recursive:
            options += " -r"

        sudo = ""
        if self.ssh_as_root == True:
            sudo = "sudo "

        self.__exec_local(f'{sudo}scp{options} {source} {self.fqdn}:{destination}')

    def __scp_from(self: 'Server', source: str, destination: str) -> None:
        """
        Copy a file from the server to the localhost.

        This method is called by the copy method if the server is not
        localhost.

        Parameters
        ----------
        source : str
            The source file.
        destination : str
            The destination file.

        Returns
        -------

        See Also
        --------
        copy : Copy a file from the server to the localhost.
        __copy_local : Copy a file from the server to the server over SSH.
        __scp_to : Copy a file from the server to the server over SSH.
        """
        options = ""
        if self.ssh_config is not None:
            options = f" -F {self.ssh_config}"

        sudo = ""
        if self.ssh_as_root == True:
            sudo = "sudo "

        self.__exec_local(f'{sudo}scp{options} {self.fqdn}:{source} {destination}')

    def copy_to(self: 'Server', source: str, destination: str, recursive: bool = False) -> None:
        """
        Copy a file from the localhost to the server.

        Parameters
        ----------
        source : str
            The source file.
        destination : str
            The destination file.

        Returns
        -------

        See Also
        --------
        __copy_local : Copy a file from the server to the server over SSH.
        __scp_to : Copy a file from the server to the server over SSH.
        copy_from : Copy a file from the server to the localhost.

        Example
        -------
        >>> server.copy_to('/home/user/file.txt', '/home/user/file.txt')
        """
        debug(f'Copying {source} to {self.log_name()}:{destination}')
        if self.localhost:
            self.__copy_local(source, destination, recursive=recursive)
        else:
            self.__scp_to(source, destination, recursive=recursive)

    def copy_from(self: 'Server', source: str, destination: str) -> None:
        """
        Copy a file from the server to the localhost.

        Parameters
        ----------
        source : str
            The source file.
        destination : str
            The destination file.

        Returns
        -------

        See Also
        --------
        __copy_local : Copy a file from the server to the server over SSH.
        __scp_from : Copy a file from the server to the server over SSH.
        copy_to : Copy a file from the localhost to the server.

        Example
        -------
        >>> server.copy_from('/home/user/file.txt', '/home/user/file.txt')
        """
        debug(f'Copying from {self.log_name()}:{source} to {destination}')
        self.__exec_local(f'mkdir -p {path_dirname(destination)} || true')
        if self.localhost:
            self.__copy_local(source, destination)
        else:
            self.__scp_from(source, destination)

    def wait_for_success(self: 'Server', command: str, timeout: int = 10
                         ) -> None:
        """
        Wait for a command to succeed.

        Parameters
        ----------
        command : str
            The command to execute.
        timeout : int
            The timeout in seconds.

        Returns
        -------

        See Also
        --------
        exec : Execute command on the server.
        """
        start = datetime.now()
        while (datetime.now() - start).total_seconds() < timeout:
            try:
                _ = self.exec(command)
                return
            except Exception:
                sleep(1)

        raise TimeoutError(f'Execution on {self.log_name()} of command ' +
                           f'{command} timed out after {timeout} seconds')

    def test(self: 'Server', command: str) -> bool:
        try:
            _ = self.exec(command)
            return True
        except CalledProcessError:
            return False

    def wait_for_connection(self: 'Server', timeout: int = 10
                            ) -> None:
        """
        Wait for the server to be connected.

        Parameters
        ----------
        timeout : int
            The timeout in seconds.

        Returns
        -------
        """
        try:
            self.wait_for_success('echo', timeout)
        except TimeoutError:
            raise TimeoutError(f'Connection attempts to {self.log_name()} ' +
                               f'timed out after {timeout} seconds')

    def get_driver_for_device(self: 'Server', device_addr: str) -> str:
        """
        Get the driver for a device.

        Parameters
        ----------
        device : str
            The device's PCI bus address.

        Returns
        -------
        str
            The driver for the device.

        See Also
        --------
        """
        return self.exec(f'lspci -v -s {device_addr} | grep driver ' +
                         '| cut -d":" -f 2 | tr -d " "').replace('\n', '')

    def get_driver_for_nic(self: 'Server', iface: str) -> str:
        """
        Get the driver for a network interface.

        Note that this does not work, once the NIC is bound to DPDK.

        Parameters
        ----------
        iface : str
            The network interface name.

        Returns
        -------
        str
            The driver for the network interface.

        See Also
        --------

        Examples
        --------
        >>> server.get_driver_for_nic('enp176s0')
        'ixgbe'
        """
        return self.get_driver_for_device(self.get_nic_pci_address(iface))

    def is_test_iface_bound(self: 'Server') -> bool:
        """
        Check if the test interface is bound to DPDK.

        Parameters
        ----------

        Returns
        -------
        bool
            True if the test interface is bound to DPDK.
        """
        return (self.get_driver_for_device(self.test_iface_addr)
                == self.test_iface_dpdk_driv)

    def bind_device(self: 'Server', dev_addr: str, driver: str) -> None:
        """
        Bind a device to a driver.

        Parameters
        ----------
        dev_addr : str
            The device's PCI bus address.
        driver : str
            The driver to bind the device to.

        Returns
        -------
        """
        cmd = f'sudo dpdk-devbind.py -b {driver} {dev_addr}'

        if self.nixos:
            _ = self.exec(f'{self.__cmd_with_package("dpdk")} sh -c "{cmd}"')
        else:
            _ = self.exec(cmd)

    def unbind_device(self: 'Server', dev_addr: str) -> None:
        """
        Unbind a device from a driver.

        Parameters
        ----------
        dev_addr : str
            The device's PCI bus address.

        Returns
        -------
        """
        cmd = f'sudo dpdk-devbind.py -u {dev_addr}'

        if self.nixos:
            _ = self.exec(f'{self.__cmd_with_package("dpdk")} sh -c "{cmd}"')
        else:
            _ = self.exec(cmd)

    def bind_nics_to_dpdk(self: 'Server') -> None:
        """
        Bind all available network interfaces to DPDK.

        Parameters
        ----------

        Returns
        -------
        """
        cmd = f'cd {self.moongen_dir}/bin/libmoon; sudo ./bind-interfaces.sh'

        if self.nixos:
            _ = self.exec(f'{self.__cmd_with_package("dpdk")} sh -c "{cmd}"')
        else:
            _ = self.exec(cmd)

    def bind_test_iface(self: 'Server') -> None:
        """
        Bind test interface to DPDK.

        Parameters
        ----------

        Returns
        -------
        """
        # detect test interface if not known
        if not (self.test_iface_addr and self.test_iface_driv):
            self.detect_test_iface()

        # check if test interface is already bound
        if self.is_test_iface_bound():
            debug(f"{self.fqdn}'s test interface already bound to DPDK.")
            if not self._test_iface_id:
                self.detect_test_iface_id()
            return

        # bind test interface to DPDK
        self.bind_device(self.test_iface_addr, self.test_iface_dpdk_driv)

        # get the test interface id
        self.detect_test_iface_id()

    def release_test_iface(self: 'Server') -> None:
        """
        Release test interface from DPDK.

        Parameters
        ----------

        Returns
        -------
        """
        self.bind_device(self.test_iface_addr, self.test_iface_driv)

    def detect_test_iface_id(self: 'Server') -> None:
        """
        Detect the test interface's DPDK ID.

        Parameters
        ----------

        Returns
        -------
        """
        cmd = ("dpdk-devbind.py -s | " +
               f"grep 'drv={self.test_iface_dpdk_driv}' || true")
        output: str
        if self.nixos:
            # nix shell prints debug output to stderr. We discard stderr.
            output = self.exec(f'{self.__cmd_with_package("dpdk")} sh -c "{cmd}" 2>/dev/null')
        else:
            output = self.exec(cmd)

        debug(f"Detecting test interface DPDK id on {self.fqdn}")

        for num, line in enumerate(output.splitlines()):
            if line.startswith(self.test_iface_addr):
                self._test_iface_id = num
                debug(f"Detected {self.fqdn}'s test interface DPDK id: {num}")
                return

        error(f"Failed to detect {self.fqdn}'s test interface DPDK id.")

    def has_pci_bus(self: 'Server') -> bool:
        """
        Check if the server has a PCI bus.

        Parameters
        ----------

        Returns
        -------
        bool
            True if the server has a PCI bus.
        """
        return self.exec('lspci')

    def detect_test_iface_by_mac(self: 'Server') -> None:
        """
        Detect the test interface by its MAC address.

        Parameters
        ----------

        Returns
        -------
        """
        output = self.exec("for d in /sys/class/net/*; " +
                           "do echo $(basename $d) $(cat $d/address); done")
        debug(f"Detecting test interface on {self.fqdn}")

        for line in output.splitlines():
            iface, mac = line.split()
            if mac != self.test_iface_mac:
                continue
            self.test_iface = iface
            debug(f"Detected {self.fqdn}'s test interface: {self.test_iface}")

            if not self.has_pci_bus():
                return

            self.test_iface_addr = self.get_nic_pci_address(self.test_iface)
            self.test_iface_driv = self.get_driver_for_nic(self.test_iface)
            return

        error(f"Failed to detect {self.fqdn}'s test interface.")

    def detect_test_iface(self: 'Server') -> None:
        """
        Detect the test interface if necessary.

        Parameters
        ----------

        Returns
        -------
        """
        if not (self.test_iface and self.test_iface_addr
                and self.test_iface_driv):
            self.detect_test_iface_by_mac()

    def setup_hugetlbfs(self: 'Server'):
        """
        Setup hugepage interface.

        Parameters
        ----------

        Returns
        -------
        """
        self.exec(
            f"cd {self.moongen_dir}/bin/libmoon; " +
            "sudo bash ./setup-hugetlbfs.sh"
        )

    def get_nic_pci_address(self: 'Server', iface: str) -> str:
        """
        Get the PCI address for a network interface.

        Note that this does not work once the NIC is bound to DPDK.

        Parameters
        ----------
        iface : str
            The network interface identifier.

        Returns
        -------
        str
            The PCI bus address.

        Example
        -------
        >>> server.get_nic_pci_address('enp176s0')
        '0000:b0:00.0'
        """
        return self.exec(
            f"basename $(realpath /sys/class/net/{iface}/device " +
            "| sed \"s/\\/virtio[0-9]//g\")"
        ).replace('\n', '')

    def get_nic_mac_address(self: 'Server', iface: str) -> str:
        """
        Get the MAC address for a network interface.

        Parameters
        ----------
        iface : str
            The network interface identifier.

        Returns
        -------
        str
            The MAC address.

        Example
        -------
        >>> server.get_nic_pci_address('enp176s0')
        '64:9d:99:b1:0b:59'
        """
        return self.exec(f'cat /sys/class/net/{iface}/address')

    def get_nic_ip_addresses(self: 'Server', iface: str) -> list[str]:
        """
        Get the IP addresses for a network interface.

        Parameters
        ----------
        iface : str
            The network interface identifier.

        Returns
        -------
        List[str]
            The IP addresses.

        Example
        -------
        >>> server.get_nic_ip_addresses('enp176s0')
        ['192.168.0.1/24']
        """
        return self.exec(f'ip addr show dev {iface}' +
                         ' | grep inet | awk "{print \\$2}"').splitlines()

    def delete_nic_ip_addresses(self: 'Server', iface: str) -> None:
        """
        Delete the IP addresses for a network interface.

        Parameters
        ----------
        iface : str
            The network interface identifier.

        Returns
        -------

        Example
        -------
        >>> server.delete_nic_ip_addresses('enp176s0')
        """
        for addr in self.get_nic_ip_addresses(iface):
            self.exec(f'sudo ip addr del {addr} dev {iface}')

    def start_moongen_reflector(self: 'Server'):
        """
        Start the libmoon L2 reflector.

        Parameters
        ----------

        Returns
        -------
        """
        self.tmux_new('reflector', f'cd {self.moongen_dir}; sudo bin/MoonGen '
                      + path_join(self.moonprogs_dir, 'reflector.lua') +
                      f' {self._test_iface_id}')

    def stop_moongen_reflector(self: 'Server'):
        """
        Stop the libmoon L2 reflector.

        Parameters
        ----------

        Returns
        -------
        """
        self.tmux_kill('reflector')

    def start_xdp_reflector(self: 'Server', iface: str = None):
        """
        Start the xdp reflector.

        Parameters
        ----------
        iface : str
            The network interface identifier.

        Returns
        -------

        Examples
        --------
        >>> server.start_xdp_reflector('enp176s0')
        """
        refl_obj_file_path = path_join(self.xdp_reflector_dir, 'reflector.o')
        if not iface:
            iface = self.test_iface
        self.exec(f'sudo ip link set {iface} xdpgeneric obj ' +
                  f'{refl_obj_file_path} sec xdp')
        self.exec(f'sudo ip link set {iface} up')

    def stop_xdp_reflector(self: 'Server', iface: str = None):
        """
        Stop the xdp reflector.

        Parameters
        ----------
        iface : str
            The network interface identifier.

        Returns
        -------

        Examples
        --------
        >>> server.stop_xdp_reflector('enp176s0')
        """
        if not iface:
            iface = self.test_iface
        self.exec(f'sudo ip link set {iface} xdpgeneric off || true')


    def start_ycsb(self, fqdn: str, rate: int = 100, runtime: int = 8, threads: int = 8, outfile: str = "/tmp/outfile.log", load: bool = False):
        """
        runtime: in secs
        load: load test data instead of running a test. Ignores many arguments.
        """
        # some general usage docs: https://github.com/brianfrankcooper/YCSB/wiki/Running-a-Workload
        # docs for generic -p parameters: https://github.com/brianfrankcooper/YCSB/wiki/Core-Properties
        # docs for redis -p parameters: https://github.com/brianfrankcooper/YCSB/blob/master/redis/README.md
        ycsb_path = f"{self.moonprogs_dir}/../../ycsb"
        workloads_path = f"{ycsb_path}/share/workloads"
        if not load:
            ops = rate * runtime
            core_properties = f"-p operationcount={ops}"
            ycsb_cmd = f'{ycsb_path}/bin/ycsb-wrapped run redis -s -P {workloads_path}/workloada -p redis.host={fqdn} -p redis.port=6379 {core_properties} -threads {threads} -target {rate}'
        else:
            ycsb_cmd = f'{ycsb_path}/bin/ycsb-wrapped load redis -s -P {workloads_path}/workloada -p redis.host={fqdn} -p redis.port=6379 -threads {threads}'
        self.tmux_new('ycsb', f'{ycsb_cmd} 2>&1 | tee {outfile}; echo AUTOTEST_DONE >> {outfile}; sleep 999');
        # error indicating strings: in live-status updates: READ-FAILED WRITE-FAILED end-report:
        # [READ], Return=ERROR


    def stop_ycsb(self):
        self.tmux_kill("ycsb")


    def upload_moonprogs(self: 'Server', source_dir: str):
        """
        Upload the MoonGen programs to the server.

        Parameters
        ----------
        source_dir : str
            The local directory containing the MoonGen programs.

        Returns
        -------
        """
        self.exec(f'mkdir -p {self.moonprogs_dir}')
        for file in listdir(source_dir):
            self.copy_to(path_join(source_dir, file), self.moonprogs_dir)

    def modprobe_test_iface_drivers(self):
        """
        Modprobe the test interface drivers.

        Parameters
        ----------

        Returns
        -------
        """
        self.exec(f'sudo modprobe {self.test_iface_driv}')

    def update_extra_hosts(self, extra_hosts: str):
        self.write(extra_hosts, "/tmp/extra_hosts")
        self.exec("rm /etc/hosts")
        self.exec("cat /etc/static/hosts > /etc/hosts")
        self.exec("cat /tmp/extra_hosts >> /etc/hosts")


class BatchExec:
    """
    Execution through ssh brings overheads of authentication etc. 
    As a mitigation, this class helps to batch commands into a single ssh command to reduce ssh-based overheads.
    """
    server: Server
    batchsize: int
    batch = []

    def __init__(self, server: Server, batchsize: int):
        self.server = server
        self.batchsize = batchsize

    def exec(self, cmd: str):
        self.batch += [cmd]
        if len(self.batch) >= self.batchsize:
            self.flush()

    def flush(self):
        if len(self.batch) == 0:
            return
        fat_cmd = "; ".join(self.batch)
        self.server.exec(fat_cmd)
        self.batch = []


class Host(Server):
    """
    Host class.

    This class represents a host, so the server that runs guest VMs. In out
    case it is also used for physical NIC tests.

    See Also
    --------
    Server : Server class.
    Guest : Guest class.
    LoadGen : LoadGen class.

    Examples
    --------
    >>> Host('server.test.de')
    Host(fqdn='server.test.de')
    """
    admin_bridge: str
    admin_bridge_ip_net: str
    admin_tap: str
    test_iface_vfio_driv: str
    test_bridge: str
    test_tap: str
    test_macvtap: str
    vmux_path: str
    vmux_socket_path: str
    guest_admin_iface_mac: str
    guest_test_iface_mac: str
    guest_root_disk_path: str
    guest_vcpus: int
    guest_memory: int
    fsdevs: dict[str, str]
    has_hugepages1g: bool

    def __init__(self: 'Host',
                 fqdn: str,
                 admin_bridge: str,
                 admin_bridge_ip_net: str,
                 admin_tap: str,
                 test_iface: str,
                 test_iface_addr: str,
                 test_iface_mac: str,
                 test_iface_driv: str,
                 test_iface_dpdk_driv: str,
                 test_iface_vfio_driv: str,
                 test_bridge: str,
                 test_bridge_ip_net: Optional[str],
                 test_tap: str,
                 test_macvtap: str,
                 vmux_path: str,
                 vmux_socket_path: str,
                 guest_root_disk_path: str,
                 guest_admin_iface_mac: str,
                 guest_test_iface_mac: str,
                 guest_vcpus: int,
                 guest_memory: int,
                 fsdevs: dict[str, str],
                 tmux_socket: str,
                 moongen_dir: str,
                 moonprogs_dir: str,
                 xdp_reflector_dir: str,
                 localhost: bool = False,
                 ssh_config: Optional[str] = None,
                 ssh_as_root: bool = False) -> None:
        """
        Initialize the Host class.

        Parameters
        ----------
        fqdn : str
            The fully qualified domain name of the host.
        admin_bridge : str
            The network interface identifier of the admin bridge interface.
        admin_bridge_ip_net : str
            The IP address and subnet mask of the admin bridge interface.
        admin_tap : str
            The network interface identifier of the admin tap interface.
        test_iface : str
            The name of the test interface.
        test_iface_addr : str
            The IP address of the test interface.
        test_iface_mac : str
            The MAC address of the test interface.
        test_iface_driv : str
            The driver of the test interface.
        test_iface_dpdk_driv : str
            The DPDK driver of the test interface.
        test_iface_vfio_driv : str
            The vfio driver of the test interface.
        test_bridge : str
            The network interface identifier of the test bridge interface.
        test_bridge_ip_net : Optional[str]
            IP net assigned to the test bridge.
        test_tap : str
            The network interface identifier of the test tap interface.
        test_macvtap : str
            The network interface identifier of the test macvtap interface.
        vmux_path : str
            Path to the vmux executable.
        vmux_socket_path : str
            The path to the vmux socket.
        guest_root_disk_path : str
            The path to the root disk of the guest.
        guest_admin_iface_mac : str
            The MAC address of the guest admin interface.
        guest_test_iface_mac : str
            The MAC address of the guest test interface.
        guest_vcpus : int
            The default number of vCPUs of the guest.
        guest_memory : int
            The default memory in MiB of the guest.
        fsdevs : dict[str, str]
            The name and path pairs for fs devices to be passed to the guest.
        tmux_socket : str
            The name for the tmux socket.
        moongen_dir : str
            The directory of the MoonGen installation.
        moonprogs_dir : str
            The directory with the MoonGen Lua programs.
        xdp_reflector_dir : str
            The directory of the xdp reflector installation.
        localhost : bool
            True if the host is localhost.
        ssh_config : Optional[str]
            Path to local ssh config to use to connect with.

        Returns
        -------
        Host : The Host object.

        See Also
        --------
        Server : The Server class.
        Server.__init__ : Initialize the Server class.

        Examples
        --------
        >>> Host('server.test.de')
        Host(fqdn='server.test.de')
        """
        super().__init__(fqdn, test_iface, test_iface_addr, test_iface_mac,
                         test_iface_driv, test_iface_dpdk_driv,
                         tmux_socket, moongen_dir, moonprogs_dir,
                         xdp_reflector_dir, localhost, ssh_config=ssh_config, 
                         ssh_as_root=ssh_as_root)
        self.admin_bridge = admin_bridge
        self.admin_bridge_ip_net = admin_bridge_ip_net
        self.admin_tap = admin_tap
        self.test_iface_vfio_driv = test_iface_vfio_driv
        self.test_bridge = test_bridge
        self.test_bridge_ip_net = test_bridge_ip_net
        self.test_tap = test_tap
        self.test_macvtap = test_macvtap
        self.vmux_path = vmux_path
        self.vmux_socket_path = vmux_socket_path
        self.guest_test_iface_mac = guest_test_iface_mac
        self.guest_admin_iface_mac = guest_admin_iface_mac
        self.guest_root_disk_path = guest_root_disk_path
        self.guest_vcpus = guest_vcpus
        self.guest_memory = guest_memory
        self.fsdevs = fsdevs
        self.has_hugepages1g = self.isdir("/dev/hugepages1G")

    def setup_admin_bridge(self: 'Host'):
        """
        Setup the admin bridge.

        Parameters
        ----------

        Returns
        -------
        """
        self.exec('sudo modprobe bridge')
        self.exec(f'sudo ip link show {self.admin_bridge} 2>/dev/null' +
                  f' || (sudo ip link add {self.admin_bridge} type bridge; ' +
                  f'sudo ip addr add {self.admin_bridge_ip_net} ' +
                  f'dev {self.admin_bridge}; true)')
        self.exec(f'sudo ip link set {self.admin_bridge} up')

    def setup_admin_tap(self: 'Host', vm_range: range = range(0)):
        """
        Setup the admin tap.

        This sets up the tap device for the admin interface of the guest VM.
        So the interface to SSH connections and stuff.

        Parameters
        ----------

        Returns
        -------
        """
        self.exec('sudo modprobe tun tap')

        buffer = BatchExec(self, 10)

        def setup(admin_tap):
            buffer.exec(f'sudo ip link show {admin_tap} 2>/dev/null' +
                      f' || (sudo ip tuntap add {admin_tap} mode tap;' +
                      f' sudo ip link set {admin_tap} '
                      f'master {self.admin_bridge}; true)')
            buffer.exec(f'sudo ip link set {admin_tap} up')

        if len(vm_range) == 0: 
            setup(MultiHost.iface_name(self.admin_tap, 0))
            buffer.flush()
            return

        for i in vm_range:
            admin_tap = MultiHost.iface_name(self.admin_tap, i)
            setup(admin_tap)

        buffer.flush()

    def modprobe_test_iface_drivers(self):
        """
        Modprobe the test interface drivers.

        Parameters
        ----------

        Returns
        -------
        """
        self.exec(f'sudo modprobe {self.test_iface_driv}')
        self.exec(f'sudo modprobe {self.test_iface_dpdk_driv}')
        self.exec(f'sudo modprobe {self.test_iface_vfio_driv}')

    def setup_test_br_tap(self: 'Host', multi_queue=True, vm_range: range = range(0)):
        """
        Setup the bridged test tap device.

        This sets up the tap device for the test interface of the guest VM.
        So the VirtIO device.

        Parameters
        ----------

        Returns
        -------
        """
        if BRIDGE_QUEUES == 0:
            multi_queue = False


        # load kernel modules
        self.exec('sudo modprobe bridge tun tap')
        username = self.whoami()

        # create bridge and add the physical NIC to it
        self.exec(f'sudo ip link show {self.test_bridge} 2>/dev/null ' +
                  f' || (sudo ip link add {self.test_bridge} type bridge; ' +
                  'true)')
        if self.test_bridge_ip_net is not None:
            self.exec(f'sudo ip addr add {self.test_bridge_ip_net} ' +
                      f'dev {self.test_bridge}; true')
        test_iface_output = self.exec(f'sudo ip link show {self.test_iface}')
        if f'master {self.test_bridge}' not in test_iface_output:
            self.exec(f'sudo ip link set {self.test_iface} ' +
                      f'master {self.test_bridge}')

        buffer = BatchExec(self, 10)

        def setup(test_tap):
            # create tap and add to bridge
            buffer.exec(f'sudo ip link show {test_tap} 2>/dev/null || ' +
                      f'(sudo ip tuntap add dev {test_tap} mode tap ' +
                      f'user {username}{" multi_queue" if multi_queue else ""}; true)')
            buffer.exec(f'if [[ "$(sudo ip link show {test_tap})" != *"master {self.test_bridge}"* ]]; then sudo ip link set {test_tap} master {self.test_bridge}; fi')
            # tap_output = self.exec(f'sudo ip link show {test_tap}')
            # if f'master {self.test_bridge}' not in tap_output:
            #     self.exec(f'sudo ip link set {test_tap} ' +
            #               f'master {self.test_bridge}')

            # bring up all interfaces (nic, bridge and tap)
            buffer.exec(f'sudo ip link set {self.test_iface} up ' +
                      f'&& sudo ip link set {self.test_bridge} up ' +
                      f'&& sudo ip link set {test_tap} up')

        if len(vm_range) == 0: 
            setup(MultiHost.iface_name(self.test_tap, 0))
            buffer.flush()
            return

        for i in vm_range:
            test_tap = MultiHost.iface_name(self.test_tap, i)
            setup(test_tap)

        buffer.flush()

        # if self.test_bridge_ip_net:
        #     self.exec(f'sudo ip address add {self.test_bridge_ip_net} dev {self.test_bridge}')

    def destroy_test_br_tap(self: 'Host'):
        """
        Destroy the bridged test tap device.

        Parameters
        ----------

        Returns
        -------
        """
        self.exec(f'sudo ip link delete {self.test_bridge} || true')
        self.exec(f'ls /sys/class/net | grep {MultiHost.iface_name(self.test_tap, -1)} | xargs -I {{}} sudo ip link delete {{}} | true')

    def setup_test_macvtap(self: 'Host'):
        """
        Setup the macvtap test interface.

        This sets up the macvtap device for the test interface of the guest VM.
        So the VirtIO device.

        Parameters
        ----------

        Returns
        -------
        """
        self.exec('sudo modprobe macvlan')
        self.exec(f'sudo ip link show {self.test_macvtap} 2>/dev/null' +
                  f' || sudo ip link add link {self.test_iface}' +
                  f' name {self.test_macvtap} type macvtap')
        self.exec(f'sudo ip link set {self.test_macvtap} address ' +
                  f'{self.guest_test_iface_mac} up')
        self.exec('sudo chmod 666' +
                  f' /dev/tap$(cat /sys/class/net/{self.test_macvtap}/ifindex)'
                  )

    def destroy_test_macvtap(self: 'Host'):
        """
        Destroy the macvtap test interface.

        Parameters
        ----------

        Returns
        -------
        """
        self.exec(f'sudo ip link delete {self.test_macvtap} || true')

    def run_guest(self: 'Host',
                  net_type: str,
                  machine_type: str,
                  vcpus: int = None,
                  memory: int = None,
                  root_disk: str = None,
                  debug_qemu: bool = False,
                  ioregionfd: bool = False,
                  qemu_build_dir: str = None,
                  vhost: bool = True,
                  rx_queue_size: int = 256,
                  tx_queue_size: int = 256,
                  vm_number: int = 0,
                  ) -> None:
        # TODO this function should get a Guest object as argument
        """
        Run a guest VM.

        Parameters
        ----------
        net_type : str
            Test interface network type
        machine_type : str
            Guest machine type
        vcpus : int
            Number of vCPUs, overwrites the default value in Host.guest_vcpus.
        memory : int
            Amount of memory in MiB, overwrites the default value in
            Host.guest_memory.
        root_disk : str
            Path to the disk file for guest's root partition
        debug_qemu : bool
            True if you want to attach GDB to Qemu. The GDB server will
            be bound to port 1234.
        ioregionfd : bool
            True if you want to use the IORegionFD enhanced virtio_net_device
            for the test interface.
        qemu_build_dir : str
            Path to the Qemu build directory. Can be empty if you want to use
            the installed Qemu.
        vhost : bool
            True if you want to use vhost on the test interface.
        rx_queue_size : int
            Size of the receive queue for the test interface.
        tx_queue_size : int
            Size of the transmit queue for the test interface.
        vm_number: int
            If not set to 0, will start VM in a way that other VMs with different vm_number can be started at the same time.

        Returns
        -------
        """
        # TODO this command should be build by the Guest object
        # it should take all the settings from the config file
        # and compile them.

        # Build misc parameters

        dev_type = 'pci' if machine_type == 'pc' else 'device'
        # TODO we need a different qemu build dir for vmux
        qemu_bin_path = 'qemu-system-x86_64'
        if qemu_build_dir:
            qemu_bin_path = path_join(qemu_build_dir, qemu_bin_path)
        cpus = vcpus if vcpus else self.guest_vcpus
        mem = memory if memory else self.guest_memory
        disk_path = self.guest_root_disk_path
        if root_disk:
            disk_path = root_disk
        disk_path = MultiHost.disk_path(disk_path, vm_number)
        fsdev_config = ''
        if self.fsdevs:
            for name, path in self.fsdevs.items():
                fsdev_config += (
                    f' -fsdev local,path={path},security_model=none,' +
                    f'id={name}fs' +
                    f' -device virtio-9p-{dev_type},mount_tag={name},' +
                    f'fsdev={name}fs'
                )

        # Build test network parameters

        test_net_config = ''
        if net_type == 'brtap':
            if BRIDGE_QUEUES == 0:
                queues = ""
                multi_queue = ""
            else:
                queues = f",queues={BRIDGE_QUEUES}"
                multi_queue = ",mq=on"
            test_net_config = (
                f" -netdev tap,vhost={'on' if vhost else 'off'}," +
                f'id=test0,ifname={MultiHost.iface_name(self.test_tap, vm_number)},script=no,' +
                f'downscript=no' +
                queues +
                f' -device virtio-net-{dev_type},id=testif,' +
                f'netdev=test0,mac={MultiHost.mac(self.guest_test_iface_mac, vm_number)}' +
                multi_queue +
                (',use-ioregionfd=true' if ioregionfd else '') +
                f',rx_queue_size={rx_queue_size},tx_queue_size={tx_queue_size}'
            )
        if net_type == 'brtap-e1000':
            test_net_config = (
                f" -netdev tap," +
                f'id=test0,ifname={MultiHost.iface_name(self.test_tap, vm_number)},script=no,' +
                'downscript=no' +
                f' -device e1000,' +
                f'netdev=test0,mac={MultiHost.mac(self.guest_test_iface_mac, vm_number)}'
            )
        elif net_type == 'macvtap':
            test_net_config = (
                f" -netdev tap,vhost={'on' if vhost else 'off'}," +
                'id=test0,fd=3 3<>/dev/tap$(cat ' +
                f'/sys/class/net/{self.test_macvtap}/ifindex) ' +
                f' -device virtio-net-{dev_type},id=testif,' +
                'netdev=test0,mac=$(cat ' +
                f'/sys/class/net/{self.test_macvtap}/address)' +
                (',use-ioregionfd=true' if ioregionfd else '') +
                f',rx_queue_size={rx_queue_size},tx_queue_size={tx_queue_size}'
            )
        elif net_type == 'vfio':
            test_net_config = f' -device vfio-pci,host={self.test_iface_addr}'
        elif net_type in [ 'vmux-pt', 'vmux-emu', 'vmux-dpdk' ]:
            test_net_config = \
                f' -device vfio-user-pci,socket={MultiHost.vfu_path(self.vmux_socket_path, vm_number)}'

        # Build memory backend parameter

        if self.has_hugepages1g:
            memory_path = f'/dev/hugepages/qemu-memory{vm_number}'
        else:
            memory_path = f'/dev/shm/qemu-{vm_number}'
        # if path_getsize(memory_path) != mem * 1024 * 1024:
        if self.test("[[ -f {memory_path} && $(stat --printf='%s' {memory_path}) -eq {mem*1024*1024} ]]"):
            self.exec(f"sudo rm {memory_path}")
        memory_backend = f' -object memory-backend-file,mem-path={memory_path},prealloc=yes,id=bm,size={mem}M,share=on'

        # Actually start qemu in tmux

        self.tmux_new(
            MultiHost.enumerate('qemu', vm_number),
            ('gdbserver 0.0.0.0:1234 ' if debug_qemu else '') +
            "sudo " +
            qemu_bin_path +
            f' -machine {machine_type}' +
            ' -cpu host' +
            f' -smp {cpus}' +

            # shared memory
            f' -m {mem}' +
            memory_backend +
            ' -numa node,memdev=bm' +

            ' -display none' + # avoid opening all the vnc ports
            ' -enable-kvm' +
            f' -drive id=root,format=qcow2,file={disk_path},'
            'if=none,cache=none' +
            f' -device virtio-blk-{dev_type},id=rootdisk,drive=root' +
            (',use-ioregionfd=true' if ioregionfd else '') +
            f',queue-size={rx_queue_size}' +
            # ' -cdrom /home/networkadmin/images/guest_init.iso' +
            f' -drive driver=raw,file={MultiHost.cloud_init(disk_path, vm_number)},if=virtio ' +
            fsdev_config +
            ' -serial stdio' +
            (' -monitor tcp:127.0.0.1:2345,server,nowait' if debug_qemu else '') +
            f' -netdev tap,vhost=on,id=admin0,ifname={MultiHost.iface_name(self.admin_tap, vm_number)},' +
            'script=no,downscript=no' +
            f' -device virtio-net-{dev_type},id=admif,netdev=admin0,' +
            f'mac={MultiHost.mac(self.guest_admin_iface_mac, vm_number)}' +
            test_net_config
            # +
            # ' -drive id=test1,format=raw,file=/dev/ssd/test1,if=none,' +
            # 'cache=none' +
            # f' -device virtio-blk-{dev_type},id=test1,drive=test1' +
            # ' -drive id=test2,format=raw,file=/dev/ssd/test2,if=none,' +
            # 'cache=none' +
            # f' -device virtio-blk-{dev_type},id=test2,drive=test2'
            # +
            # ' --trace virtio_mmio_read --trace virtio_mmio_write' +
            +
            f' 2>/tmp/trace-vm{vm_number}.log'
            )

    def kill_guest(self: 'Host') -> None:
        """
        Kill all guest VMs.

        Parameters
        ----------

        Returns
        -------
        """
        self.tmux_kill('qemu')

    def start_vmux(self: 'Host', interface: str, num_vms: int = 0) -> None:
        """
        Start vmux in a tmux session.

        Parameters
        ----------

        Returns
        -------
        """
        args = ""
        dpdk_args = ""
        if interface == "vmux-pt":
            args = f' -s {self.vmux_socket_path} -d {self.test_iface_addr}'
        if interface == "vmux-dpdk":
            dpdk_args += " -u -- -l 1 -n 1"
        if interface in [ "vmux-emu", "vmux-dpdk"]:
            if num_vms == 0:
                args = f' -s {self.vmux_socket_path} -d none -t {MultiHost.iface_name(self.test_tap, 0)} -m e1000-emu'
            else:
                for vm_number in MultiHost.range(num_vms):
                    args += f' -s {MultiHost.vfu_path(self.vmux_socket_path, vm_number)} -d none -t {MultiHost.iface_name(self.test_tap, vm_number)} -m e1000-emu'

        base_mac = MultiHost.mac(self.guest_test_iface_mac, 1) # vmux increments macs itself
        self.tmux_new(
            'vmux',
            f'ulimit -n 4096; sudo {self.vmux_path} -q -b {base_mac}'
            f'{args}'
            f'{dpdk_args}'
            # f' -d none -t tap-okelmann02 -m e1000-emu -s /tmp/vmux-okelmann.sock2'
            f'; sleep 999'
        )
        sleep(1); # give vmux some time to start up and create the socket
        self.exec(f'sudo chmod 777 {self.vmux_socket_path} || true') # this should be applied to all MultiHost.vfu_paths, but usually we dont need it anyways

    def stop_vmux(self: 'Host') -> None:
        """
        Stop vmux.

        Parameters
        ----------

        Returns
        -------
        """
        self.tmux_kill('vmux')

    def cleanup_network(self: 'Host', number_vms: int = MAX_VMS) -> None:
        """
        Cleanup the network setup.

        Parameters
        ----------

        Returns
        -------
        """
        try:
            self.stop_vmux()
        except:
            pass
        self.modprobe_test_iface_drivers()
        self.release_test_iface()
        self.stop_xdp_reflector(self.test_iface)
        self.destroy_test_br_tap()
        self.destroy_test_macvtap()
        # TODO destroy admin interfaces!


class Guest(Server):
    """
    Guest class.

    This class represents a guest, so the VM run on the host.

    See Also
    --------
    Server : The Server class.
    Host : The Host class.
    LoadGen : The LoadGen class.

    Examples
    --------
    >>> Guest('server.test.de')
    Guest(fqdn='server.test.de')
    """
    test_iface_ip_net: Optional[str]

    def __init__(self: 'Guest',
                 fqdn: str,
                 test_iface: str,
                 test_iface_addr: str,
                 test_iface_mac: str,
                 test_iface_ip_net: Optional[str],
                 test_iface_driv: str,
                 test_iface_dpdk_driv: str,
                 tmux_socket: str,
                 moongen_dir: str,
                 moonprogs_dir: str,
                 xdp_reflector_dir: str,
                 ssh_config: Optional[str] = None,
                 ssh_as_root: bool = False) -> None:
        """
        Initialize the Guest class.

        Parameters
        ----------
        fqdn : str
            The fully qualified domain name of the guest.
        test_iface : str
            The name of the test interface.
        test_iface_addr : str
            The IP address of the test interface.
        test_iface_mac : str
            The MAC address of the test interface.
        test_iface_driv : str
            The driver of the test interface.
        test_iface_dpdk_driv : str
            The DPDK driver of the test interface.
        tmux_socket : str
            The name for the tmux socket.
        moongen_dir : str
            The directory of the MoonGen installation.
        moonprogs_dir : str
            The directory with the MoonGen Lua programs.
        xdp_reflector_dir : str
            The directory of the XDP Reflector installation.
        localhost : bool
            True if the host is localhost.
        ssh_config : Optional[str]
            Path to local ssh config to use to connect with.

        Returns
        -------
        Guest : The Guest object.

        See Also
        --------
        Server : The Server class.
        Server.__init__ : Initialize the Server class.

        Examples
        --------
        >>> Guest('server.test.de')
        Guest(fqdn='server.test.de')
        """
        super().__init__(fqdn, test_iface, test_iface_addr, test_iface_mac,
                         test_iface_driv, test_iface_dpdk_driv,
                         tmux_socket, moongen_dir, moonprogs_dir,
                         xdp_reflector_dir, ssh_config=ssh_config,
                         ssh_as_root=ssh_as_root)
        self.test_iface_ip_net = test_iface_ip_net

    def __post_init__(self: 'Guest') -> None:
        """
        Post initialization.

        This method is called after the object is created.

        Parameters
        ----------

        Returns
        -------

        See Also
        --------
        __init__ : Initialize the object.
        """
        super().__post_init__()

        # Due to the write lock issue with the /nix/store we cannot do
        # stuff like opening nix shells. So we make sure the guest can
        # run commands without that, and therefore unset the nixos
        # attribute.
        self.nixos = False

    def multihost_clone(self: 'Guest', vm_number: int) -> 'Guest':
        guest = copy.deepcopy(self)
        guest.fqdn = MultiHost.ssh_hostname(guest.fqdn, vm_number)
        if guest.test_iface_ip_net is not None:
            guest.test_iface_ip_net = MultiHost.ip(guest.test_iface_ip_net, vm_number)
        return guest

    def setup_test_iface_ip_net(self: 'Guest'):
        """
        Assign ip address and netmask to the test interface if it exists.

        Parameters
        ----------

        Returns
        -------
        """
        # sometimes the VM needs a bit of extra time until it can assign an IP
        self.wait_for_success(f'sudo ip address add {self.test_iface_ip_net} dev {self.test_iface} 2>&1 | tee /tmp/foo')


class LoadGen(Server):
    """
    LoadGen class.

    This class represents a loadgen server, so the server that runs the load
    generator against the host and guest.

    See Also
    --------
    Server : The Server class.
    Host : The Host class.
    Guest : The Guest class.

    Examples
    --------
    >>> LoadGen('server.test.de')
    LoadGen(fqdn='server.test.de')
    """
    test_iface_ip_net: Optional[str]

    def __init__(self: 'LoadGen',
                 fqdn: str,
                 test_iface: str,
                 test_iface_addr: str,
                 test_iface_mac: str,
                 test_iface_ip_net: Optional[str],
                 test_iface_driv: str,
                 tmux_socket: str,
                 moongen_dir: str,
                 moonprogs_dir: str,
                 xdp_reflector_dir: Optional[str] = None,
                 localhost: bool = False,
                 ssh_config: Optional[str] = None,
                 ssh_as_root: bool = False,
                 **_kwargs) -> None:
        """
        Initialize the LoadGen class.

        Parameters
        ----------
        fqdn : str
            The fully qualified domain name of the load generator.
        test_iface : str
            The name of the test interface.
        test_iface_addr : str
            The IP address of the test interface.
        test_iface_mac : str
            The MAC address of the test interface.
        test_iface_driv : str
            The driver of the test interface.
        tmux_socket : str
            The name for the tmux socket.
        moongen_dir : str
            The directory of the MoonGen installation.
        moonprogs_dir : str
            The directory with the MoonGen Lua programs.
        xdp_reflector_dir : str
            The directory of the XDP Reflector installation.
        localhost : bool
            True if the host is localhost.
        ssh_config : Optional[str]
            Path to local ssh config to use to connect with.

        Returns
        -------
        LoadGen : The LoadGen object.

        See Also
        --------
        Server : The Server class.
        Server.__init__ : Initialize the Server class.

        Examples
        --------
        >>> LoadGen('server.test.de')
        LoadGen(fqdn='server.test.de')
        """
        super().__init__(fqdn, test_iface, test_iface_addr, test_iface_mac,
                         test_iface_driv, tmux_socket, moongen_dir,
                         moonprogs_dir, xdp_reflector_dir, localhost,
                         ssh_config=ssh_config, ssh_as_root=ssh_as_root)
        self.test_iface_ip_net = test_iface_ip_net

    @staticmethod
    def run_l2_load_latency(server: Server,
                            mac: str,
                            rate: int = 10000,
                            runtime: int = 60,
                            size: int = 60,
                            histfile: str = 'histogram.csv',
                            outfile: str = 'output.log'
                            ):
        """
        Run the MoonGen L2 load latency test.

        Parameters
        ----------
        mac : str
            The MAC address of the destination device.
        rate : int
            The rate of the test in Mbps.
        runtime : int
            The runtime of the test in seconds.
        size : int
            The size of the packets in bytes.
        histfile : str
            The path of the histogram file.
        outfile : str
            The path of the output file.

        Returns
        -------

        See Also
        --------

        Example
        -------
        >>> LoadGen('server.test.de').start_l2_load_latency()
        """
        server.tmux_new('loadlatency', f'cd {server.moongen_dir}; ' +
                      'sudo bin/MoonGen '
                      f'{server.moonprogs_dir}/l2-load-latency.lua ' +
                      f'-r {rate} -f {histfile} -T {runtime} -s {size} ' +
                      f'{server._test_iface_id} {mac} ' +
                      f'2>&1 | tee {outfile}')

    @staticmethod
    def stop_l2_load_latency(server: 'Server'):
        """
        Stop the MoonGen L2 load latency test.

        Parameters
        ----------

        Returns
        -------
        """
        server.tmux_kill('loadlatency')

    @staticmethod
    def start_wrk2(server: 'Server', url: str, script_path: str, duration: int = 11, rate: int = 10, connections: int = 16, threads: int = 8, outfile: str = "/tmp/outfile.log", workdir: str ="./"):
        docker_cmd = f'docker run -ti --mount type=bind,source=./wrk2,target=/wrk2 --network host wrk2d'
        wrk_cmd = f'wrk -D exp -t {threads} -c {connections} -d {duration} -L -s {script_path} http://{url} -R {rate}'
        server.tmux_new('wrk2', f'cd {workdir}; ' +
                        f'{docker_cmd} {wrk_cmd} 2>&1 | tee {outfile}; echo AUTOTEST_DONE >> {outfile}; sleep 999');

    @staticmethod
    def stop_wrk2(server: 'Server'):
        server.tmux_kill('wrk2')

    def start_redis(self):
        project_root = str(Path(self.moonprogs_dir) / "../..") # nix wants nicely formatted paths
        redis_cmd = f"redis-server \\*:6379 --protected-mode no"
        nix_cmd = f"nix shell --inputs-from {project_root} nixpkgs#redis --command {redis_cmd}"
        self.tmux_new("redis", f"{nix_cmd}; sleep 999")


    def stop_redis(self):
        self.tmux_kill("redis")

    def setup_test_iface_ip_net(self: 'LoadGen'):
        """
        Assign ip address and netmask to the test interface if it exists.

        Parameters
        ----------

        Returns
        -------
        """
        self.exec(f'sudo ip link set {self.test_iface} up')
        self.exec(f'sudo ip address add {self.test_iface_ip_net} dev {self.test_iface}')
