#!/usr/bin/env python3

# cd to this files location
from pathlib import Path
from typing import Optional, TypeVar
import os
import time
CALLEE_DIR = os.getcwd()
ROOT = Path(__file__).parent.resolve()
os.chdir(ROOT)


U = TypeVar('U')
def unwrap(a: Optional[U]) -> U: 
    assert a is not None
    return a


# import dpdk helper code
import importlib.util
spec = unwrap(importlib.util.spec_from_file_location("dpdk_devbind", "../mg21/bin/libmoon/deps/dpdk/usertools/dpdk-devbind.py"))
dpdk_devbind = importlib.util.module_from_spec(spec)
unwrap(spec.loader).exec_module(dpdk_devbind)

import yaml
import sys
from typing import Callable
import subprocess

def dpdk_devbind_init() -> None:
    dpdk_devbind.clear_data()
    dpdk_devbind.check_modules()
    dpdk_devbind.get_device_details(dpdk_devbind.network_devices)
    dpdk_devbind.get_device_details(dpdk_devbind.baseband_devices)
    dpdk_devbind.get_device_details(dpdk_devbind.crypto_devices)
    dpdk_devbind.get_device_details(dpdk_devbind.dma_devices)
    dpdk_devbind.get_device_details(dpdk_devbind.eventdev_devices)
    dpdk_devbind.get_device_details(dpdk_devbind.mempool_devices)
    dpdk_devbind.get_device_details(dpdk_devbind.compress_devices)
    dpdk_devbind.get_device_details(dpdk_devbind.regex_devices)
    dpdk_devbind.get_device_details(dpdk_devbind.misc_devices)

def dpdk_devbind_print():
    dpdk_devbind_init()
    dpdk_devbind.status_dev = "all"
    dpdk_devbind.show_status()
    pass

def dpdk_devbind_bind(dev_id: str, driver: str) -> None:
    dpdk_devbind_init()
    dpdk_devbind.bind_all([dev_id], driver)

def modprobe(arg: str):
    subprocess.run(["modprobe", arg], check=True);
    time.sleep(1) # could this help to prevent "Error: Driver 'vfio-pci' is not loaded."?


def applyDevice(devYaml: str) -> None:
    """
    bind device to expected driver
    """
    modprobe(devYaml['dpdk-driver'])
    dpdk_devbind_bind(devYaml['pci'], devYaml['dpdk-driver'])
    print(f"Binding dpdk-driver: {devYaml['pci']} <- {devYaml['dpdk-driver']}")

def checkDeviceConfig(devYaml: str) -> None:
    """
    checks expected pci id and firmware version
    """
    modprobe(devYaml['kernel-driver'])
    dpdk_devbind_bind(devYaml['pci'], devYaml['kernel-driver'])
    info = subprocess.run(["ethtool", "-i", devYaml['if']], check=True, capture_output=True).stdout
    # print(f"ethtool: {info}")
    info = info.split(b'\n')
    firmware_version = info[2].split(b'firmware-version: ')[1].decode('utf-8')
    assert firmware_version in devYaml['firmware-versions'], \
            f"Firmware version {firmware_version} does not match the expected ones from the yaml."
    bus_info = info[4].split(b'bus-info: ')[1].decode('utf-8')
    assert devYaml['pci'] in bus_info, \
            f"PCI bus of {devYaml['if']} is {bus_info} instead of what the yaml expects."
    print(f"device check ok for {bus_info}")


def apply(hostcfg: str, function: Callable[[str], None]) -> None:
    devsYaml = hostcfg['devices']
    ethLoadgen = next((x for x in devsYaml if x['name'] == "ethLoadgen"), None)
    if ethLoadgen is not None:
        function(ethLoadgen)
    ethDut = next((x for x in devsYaml if x['name'] == "ethDut"), None)
    if ethDut is not None:
        function(ethDut)

def checkIommu(hostcfg: str) -> None:
    intel_iommu_on = os.path.isdir("/sys/devices/virtual/iommu")
    amd_iommu_on = os.path.isdir("/sys/devices/amd_iommu_0")
    iommu_on = intel_iommu_on or amd_iommu_on
    assert iommu_on == hostcfg['iommu_on'], f"Iommu_is_on = {iommu_on} which is not what config requires"

def checkHugepages(hostcfg: str) -> None:
    with open("/sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages", "r") as f:
        pages1G = int(f.readline())
        required = hostcfg['hugepages1G']
        assert pages1G == required, f"{pages1G} 1G hugepages instead of {required} found"

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description='Apply host yamls.')
    parser.add_argument('file', type=str, 
                        help='The yaml file to apply')
    args = parser.parse_args()
    yamlPath = Path(CALLEE_DIR)
    yamlPath /= args.file

    with open(yamlPath, 'r') as file:
        hostcfg = yaml.safe_load(file)
        checkIommu(hostcfg)
        checkHugepages(hostcfg)
        print("host config ok")
        apply(hostcfg, checkDeviceConfig)
        apply(hostcfg, applyDevice)
        # dpdk_devbind_print()


