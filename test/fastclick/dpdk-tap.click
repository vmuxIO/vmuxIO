define(
	$ifacePCI0 0000:00:06.0,
	$macAddress 52:54:00:fa:00:60,
	$ipAddress 10.100.100.1/30,
	$devName tapVmux
);

tap :: KernelTap($ipAddress, ETHER $macAddress, DEVNAME $devName);

FromDPDKDevice($ifacePCI0) -> tap;
tap -> ToDPDKDevice($ifacePCI0);
