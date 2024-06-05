/**
 * A NIC offloading example using DPDK's Flow API.
 * Three NIC rules are offloaded, each dispatching matched packets
 * to one of the first 3 NIC queues. Another 2 queues are used by RSS.
 */

/**
 * Deploy as follows:
 * sudo ../../bin/click --dpdk -l 0-4 -w 0000:11:00.0 -v -- dpdk-flow-parser.click rules=./path/to/rulesfile
 */

define(
	// $ifacePCI0  0000:41:00.0,
	$ifacePCI0  0000:00:06.0,
	$queues     3,
	$threads    $queues,
	$numa       false,
	$mode       flow,    // Rx mode required by the DPDK Flow Rule Manager
	$verbose    99,
	$rules      ./test/fastclick/test_dpdk_nic_rules        // Better provide absolute path
);

// NIC in Flow Rule Manager's mode
fd0 :: FromDPDKDevice(
	PORT $ifacePCI0, MODE $mode,
	N_QUEUES $queues, NUMA $numa,
	THREADOFFSET 0, MAXTHREADS $threads,
	FLOW_RULES_FILE $rules,
	VERBOSE $verbose
	// , PAUSE full // according to click docs this is needed for rte_flow but i claim it is entirely unrelated
);

fd0
  -> dropped :: AverageCounterIMP()
  -> Discard;

DriverManager(
	pause,
	print "",
	print "Dropped: "$(dropped.count),
	stop
);
