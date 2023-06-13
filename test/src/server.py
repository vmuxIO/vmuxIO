from dataclasses import dataclass, field
from subprocess import check_output, CalledProcessError, STDOUT
from socket import getfqdn
from logging import debug, warning, error
from time import sleep
from datetime import datetime
from abc import ABC
from os import listdir
from os.path import join as path_join
from os.path import dirname as path_dirname
from typing import Optional


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
        self.localhost = self.fqdn == 'localhost' or self.fqdn == getfqdn()
        try:
            self.nixos = self.isfile('/etc/NIXOS')
        except Exception:
            warning(f'Could not run nixos detection on {self.fqdn}')

    def hostname(self: 'Server') -> str:
        """
        Get the hostname of the server.

        Parameters
        ----------

        Returns
        -------
        str
            The hostname of the server.
        """
        return self.fqdn.split('.')[0]

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
        return f'{self.__class__.__name__.lower()} {self.hostname()}'

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

        return check_output(f"ssh{options} {self.fqdn} '{command}'",
                            stderr=STDOUT, shell=True).decode('utf-8')

    def exec(self: 'Server', command: str) -> str:
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
        debug(f'Executing command on {self.log_name()}: {command}')
        if self.localhost:
            return self.__exec_local(command)
        else:
            return self.__exec_ssh(command)

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
        Stop a tmux session on the server.

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
                      f'| grep {session_name} | xargs ' +
                      f'tmux -L {self.tmux_socket} kill-session -t || true')

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

    def __copy_local(self: 'Server', source: str, destination: str) -> None:
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
        self.__exec_local(f'mkdir -p {path_dirname(destination)}')
        self.__exec_local(f'cp {source} {destination}')

    def __scp_to(self: 'Server', source: str, destination: str) -> None:
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
        self.__exec_local(f'scp {source} {self.fqdn}:{destination}')

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
        self.__exec_local(f'scp {self.fqdn}:{source} {destination}')

    def copy_to(self: 'Server', source: str, destination: str) -> None:
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
            self.__copy_local(source, destination)
        else:
            self.__scp_to(source, destination)

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
            _ = self.exec(f'nix-shell -p dpdk --run "{cmd}"')
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
            _ = self.exec(f'nix-shell -p dpdk --run "{cmd}"')
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
            _ = self.exec(f'nix-shell -p dpdk --run "{cmd}"')
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
            output = self.exec(f'nix-shell -p dpdk --run "{cmd}"')
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
    test_iface_vfio_driv: str
    test_bridge: str
    vmux_path: str
    vmux_socket_path: str

    def __init__(self: 'Host',
                 fqdn: str,
                 admin_bridge: str,
                 admin_bridge_ip_net: str,
                 test_iface: str,
                 test_iface_addr: str,
                 test_iface_mac: str,
                 test_iface_driv: str,
                 test_iface_dpdk_driv: str,
                 test_iface_vfio_driv: str,
                 test_bridge: str,
                 vmux_path: str,
                 vmux_socket_path: str,
                 tmux_socket: str,
                 moongen_dir: str,
                 moonprogs_dir: str,
                 xdp_reflector_dir: str,
                 localhost: bool = False,
                 ssh_config: Optional[str] = None) -> None:
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
        vmux_path : str
            Path to the vmux executable.
        vmux_socket_path : str
            The path to the vmux socket.
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
                         xdp_reflector_dir, localhost, ssh_config=ssh_config)
        self.admin_bridge = admin_bridge
        self.admin_bridge_ip_net = admin_bridge_ip_net
        self.test_iface_vfio_driv = test_iface_vfio_driv
        self.test_bridge = test_bridge
        self.vmux_path = vmux_path
        self.vmux_socket_path = vmux_socket_path

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

    def setup_admin_tap(self: 'Host', guest: 'Guest'):
        """
        Setup the admin tap.

        This sets up the tap device for the admin interface of the guest VM.
        So the interface to SSH connections and stuff.

        Parameters
        ----------

        Returns
        -------
        """
        # TODO this should use guest information
        self.exec('sudo modprobe tun tap')
        self.exec(f'sudo ip link show {guest.admin_tap} 2>/dev/null' +
                  f' || (sudo ip tuntap add {guest.admin_tap} mode tap;' +
                  f' sudo ip link set {guest.admin_tap} '
                  f'master {self.admin_bridge}; true)')
        self.exec(f'sudo ip link set {guest.admin_tap} up')

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

    def setup_test_bridge(self: 'Host'):
        """
        Setup the bridge for test bridge tap device.

        This sets up the bridge for the test tap interface of the guest VM.
        So the VirtIO device.

        Parameters
        ----------

        Returns
        -------
        """
        # load kernel modules
        self.exec('sudo modprobe bridge tun tap')

        # create bridge and tap device
        self.exec(f'sudo ip link show {self.test_bridge} 2>/dev/null ' +
                  f' || (sudo ip link add {self.test_bridge} type bridge; ' +
                  'true)')

        # bring up test interface and bridge
        self.exec(f'sudo ip link set {self.test_iface} up')

    def setup_test_tap(self: 'Host', guest: 'Guest'):
        """
        Setup the bridged test tap device.

        This sets up the tap device for the test interface of the guest VM.
        So the VirtIO device.

        Parameters
        ----------

        Returns
        -------
        """
        # TODO this should use guest information

        # load kernel modules
        self.exec('sudo modprobe bridge tun tap')

        # create bridge and tap device
        self.exec(f'sudo ip link show {self.test_bridge} 2>/dev/null ' +
                  f' || (sudo ip link add {self.test_bridge} type bridge; ' +
                  'true)')
        username = self.whoami()
        self.exec(f'sudo ip link show {guest.test_tap} 2>/dev/null || ' +
                  f'(sudo ip tuntap add dev {guest.test_tap} mode tap ' +
                  f'user {username} multi_queue; true)')

        # add tap device and physical nic to bridge
        tap_output = self.exec(f'sudo ip link show {guest.test_tap}')
        if f'master {self.test_bridge}' not in tap_output:
            self.exec(f'sudo ip link set {guest.test_tap} ' +
                      f'master {self.test_bridge}')
        test_iface_output = self.exec(f'sudo ip link show {self.test_iface}')
        if f'master {self.test_bridge}' not in test_iface_output:
            self.exec(f'sudo ip link set {self.test_iface} ' +
                      f'master {self.test_bridge}')

        # bring up all interfaces (nic, bridge and tap)
        self.exec(f'sudo ip link set {self.test_iface} up ' +
                  f'&& sudo ip link set {self.test_bridge} up ' +
                  f'&& sudo ip link set {guest.test_tap} up')

    def destroy_test_tap(self: 'Host', guest: 'Guest'):
        """
        Destroy the bridged test tap device.

        Parameters
        ----------

        Returns
        -------
        """
        # TODO this function should use guest information
        self.exec(f'sudo ip link delete {guest.test_tap} || true')

    def destroy_test_bridge(self: 'Host'):
        """
        Destroy the test bridge.

        Parameters
        ----------

        Returns
        -------
        """
        self.exec(f'sudo ip link delete {self.test_bridge} || true')

    def setup_test_macvtap(self: 'Host', guest: 'Guest'):
        """
        Setup the macvtap test interface.

        This sets up the macvtap device for the test interface of the guest VM.
        So the VirtIO device.

        Parameters
        ----------

        Returns
        -------
        """
        # TODO this should use guest information
        self.exec('sudo modprobe macvlan')
        self.exec(f'sudo ip link show {guest.test_macvtap} 2>/dev/null' +
                  f' || sudo ip link add link {self.test_iface}' +
                  f' name {guest.test_macvtap} type macvtap')
        self.exec(f'sudo ip link set {guest.test_macvtap} address ' +
                  f'{guest.test_iface_mac} up')
        self.exec('sudo chmod 666 /dev/tap$(cat ' +
                  f'/sys/class/net/{guest.test_macvtap}/ifindex)')

    def destroy_test_macvtap(self: 'Host', guest: 'Guest'):
        """
        Destroy the macvtap test interface.

        Parameters
        ----------

        Returns
        -------
        """
        # TODO this should use guest information
        self.exec(f'sudo ip link delete {guest.test_macvtap} || true')

    def run_guest(self: 'Host',
                  guest: 'Guest',
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
                  ) -> None:
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

        Returns
        -------
        """
        # TODO this should use guest information
        # TODO this command should be build by the Guest object
        # it should take all the settings from the config file
        # and compile them.
        dev_type = 'pci' if machine_type == 'pc' else 'device'
        test_net_config = ''
        if net_type == 'brtap':
            test_net_config = (
                f" -netdev tap,vhost={'on' if vhost else 'off'}," +
                f'id=admin1,ifname={guest.test_tap},script=no,' +
                'downscript=no,queues=4' +
                f' -device virtio-net-{dev_type},id=testif,' +
                f'netdev=admin1,mac={guest.test_iface_mac},mq=on' +
                (',use-ioregionfd=true' if ioregionfd else '') +
                f',rx_queue_size={rx_queue_size},tx_queue_size={tx_queue_size}'
            )
        elif net_type == 'macvtap':
            test_net_config = (
                f" -netdev tap,vhost={'on' if vhost else 'off'}," +
                'id=admin1,fd=3 3<>/dev/tap$(cat ' +
                f'/sys/class/net/{guest.test_macvtap}/ifindex) ' +
                f' -device virtio-net-{dev_type},id=testif,' +
                'netdev=admin1,mac=$(cat ' +
                f'/sys/class/net/{guest.test_macvtap}/address)' +
                (',use-ioregionfd=true' if ioregionfd else '') +
                f',rx_queue_size={rx_queue_size},tx_queue_size={tx_queue_size}'
            )
        elif net_type == 'vfio':
            test_net_config = f' -device vfio-pci,host={self.test_iface_addr}'
        elif net_type == 'vmux':
            test_net_config = \
                f' -device vfio-user-pci,socket={self.vmux_socket_path}'

        # TODO we need a different qemu build dir for vmux
        qemu_bin_path = 'qemu-system-x86_64'
        if qemu_build_dir:
            qemu_bin_path = path_join(qemu_build_dir, qemu_bin_path)
        cpus = vcpus if vcpus else guest.vcpus
        mem = memory if memory else guest.memory
        disk_path = guest.root_disk_path
        if root_disk:
            disk_path = root_disk
        fsdev_config = ''
        if guest.fsdevs:
            for name, path in guest.fsdevs.items():
                fsdev_config += (
                    f' -fsdev local,path={path},security_model=none,' +
                    f'id={name}fs' +
                    f' -device virtio-9p-{dev_type},mount_tag={name},' +
                    f'fsdev={name}fs'
                )
        self.tmux_new(
            # TODO tmux session needs a guest specific name
            'qemu',
            ('gdbserver 0.0.0.0:1234 ' if debug_qemu else '') +
            qemu_bin_path +
            f' -machine {machine_type}' +
            ' -cpu host' +
            f' -smp {cpus}' +
            f' -m {mem}' +
            ' -enable-kvm' +
            f' -drive id=root,format=qcow2,file={disk_path},'
            'if=none,cache=none' +
            f' -device virtio-blk-{dev_type},id=rootdisk,drive=root' +
            (',use-ioregionfd=true' if ioregionfd else '') +
            f',queue-size={rx_queue_size}' +
            # ' -cdrom /home/networkadmin/images/guest_init.iso' +
            fsdev_config +
            ' -serial stdio' +
            ' -monitor tcp:127.0.0.1:2345,server,nowait' +
            f' -netdev tap,vhost=on,id=admin0,ifname={guest.admin_tap},' +
            'script=no,downscript=no' +
            f' -device virtio-net-{dev_type},id=admif,netdev=admin0,' +
            f'mac={guest.admin_iface_mac}' +
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
            # ' 2>trace.log'
            )

    def kill_guest(self: 'Host', guest: 'Guest') -> None:
        """
        Kill a guest VM.

        Parameters
        ----------

        Returns
        -------
        """
        # TODO this should use guest information
        # TODO tmux session needs a guest specific name
        self.tmux_kill('qemu')

    def start_vmux(self: 'Host') -> None:
        """
        Start vmux in a tmux session.

        Parameters
        ----------

        Returns
        -------
        """
        self.tmux_new('vmux', f'ulimit -n 4096; sudo {self.vmux_path}')

    def stop_vmux(self: 'Host') -> None:
        """
        Stop vmux.

        Parameters
        ----------

        Returns
        -------
        """
        self.tmux_kill('vmux')

    def cleanup_network(self: 'Host', guest: 'Guest') -> None:
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
        self.release_test_iface()
        self.stop_xdp_reflector(self.test_iface)
        self.destroy_test_tap(guest)
        self.destroy_test_bridge()
        self.destroy_test_macvtap(guest)


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
    vcpus: int
    memory: int
    admin_tap: str
    admin_iface_mac: str
    test_tap: str
    test_macvtap: str
    root_disk_path: str
    fsdevs: dict[str, str]

    def __init__(self: 'Guest',
                 fqdn: str,
                 vcpus: int,
                 memory: int,
                 admin_tap: str,
                 admin_iface_mac: str,
                 test_iface: str,
                 test_iface_addr: str,
                 test_iface_mac: str,
                 test_iface_driv: str,
                 test_iface_dpdk_driv: str,
                 test_tap: str,
                 test_macvtap: str,
                 root_disk_path: str,
                 tmux_socket: str,
                 moongen_dir: str,
                 moonprogs_dir: str,
                 xdp_reflector_dir: str,
                 fsdevs: dict[str, str],
                 ssh_config: Optional[str] = None,
                 ) -> None:
        """
        Initialize the Guest class.

        Parameters
        ----------
        fqdn : str
            The fully qualified domain name of the guest.
        vcpus : int
            The default number of vCPUs of the guest.
        memory : int
            The default memory in MiB of the guest.
        admin_tap : str
            The network interface identifier of the admin tap interface.
        admin_iface_mac : str
            The MAC address of the guest admin interface.
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
        test_tap : str
            The network interface identifier of the test tap interface.
        test_macvtap : str
            The network interface identifier of the test macvtap interface.
        root_disk_path : str
            The path to the root disk of the guest.
        tmux_socket : str
            The name for the tmux socket.
        moongen_dir : str
            The directory of the MoonGen installation.
        moonprogs_dir : str
            The directory with the MoonGen Lua programs.
        xdp_reflector_dir : str
            The directory of the XDP Reflector installation.
        fsdevs : dict[str, str]
            The name and path pairs for fs devices to be passed to the guest.
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
                         xdp_reflector_dir, ssh_config=ssh_config)
        self.vcpus = vcpus
        self.memory = memory
        self.admin_tap = admin_tap
        self.admin_iface_mac = admin_iface_mac
        self.test_tap = test_tap
        self.test_macvtap = test_macvtap
        self.root_disk_path = root_disk_path
        self.fsdevs = fsdevs

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

    def __init__(self: 'LoadGen',
                 fqdn: str,
                 test_iface: str,
                 test_iface_addr: str,
                 test_iface_mac: str,
                 test_iface_driv: str,
                 tmux_socket: str,
                 moongen_dir: str,
                 moonprogs_dir: str,
                 xdp_reflector_dir: Optional[str] = None,
                 localhost: bool = False,
                 ssh_config: Optional[str] = None) -> None:
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
                         ssh_config=ssh_config)

    def run_l2_load_latency(self: 'LoadGen',
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
        self.tmux_new('loadlatency', f'cd {self.moongen_dir}; ' +
                      'sudo bin/MoonGen '
                      f'{self.moonprogs_dir}/l2-load-latency.lua ' +
                      f'-r {rate} -f {histfile} -T {runtime} -s {size} ' +
                      f'{self._test_iface_id} {mac} ' +
                      f'2>&1 | tee {outfile}')

    def stop_l2_load_latency(self: 'Server'):
        """
        Stop the MoonGen L2 load latency test.

        Parameters
        ----------

        Returns
        -------
        """
        self.tmux_kill('loadlatency')
