# # Test setup documentation

The test setups use two servers, the Host and the LoadGen.

The host is responsible for hosting one or more virtual machines which start vmux instances.
The Loadgen uses some test specific tool to generate packets to send to the VM.

The test setups execute shell commands by connecting to the respective server / VM using SSH.


## VNF test (measure_vnf.py)

Creates a single VM on the Host and uses moongen to generate packets to send to the VM.

On the Loadgen, the reflector script is started, which reflects all incoming packets back to their source. The packets are generated in batches on the VM, by test/kni-latency. In this case, the VM acts as the load generator.

The latency for a roundtrip each batch of packets is measured and outputted in /tmp/out1/measure_vnf_rep0.logon the Loadgen.


## Iperf test (measure_iperf.py)

Creates a single VM on the Host and uses iperf to test the maximum bandwidth of the connection.

The Loadgen acts as the iperf client and sends packets to the guest VM which acts as the server. The results are saved to a file on the Loadgen.

## Hotel test (measure_hotel.py)


## ycsb test (measure_ycsb.py)