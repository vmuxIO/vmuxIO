tap :: KernelTap(10.100.100.1/30);

FromDPDKDevice(0) -> tap;
tap -> ToDPDKDevice(0);
