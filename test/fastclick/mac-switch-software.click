/**
 * A NIC offloading example using DPDK's Flow API.
 * Three NIC rules are offloaded, each dispatching matched packets
 * to one of the first 3 NIC queues. Another 2 queues are used by RSS.
 */

/**
 * Deploy as follows:
 * sudo ../../bin/click --dpdk -l 0-4 -w 0000:11:00.0 -v -- dpdk-flow-parser.click
 */

define(
	// $ifacePCI0  0000:41:00.0,
	$ifacePCI0  0000:00:06.0,
	$queues     3,
	$threads    $queues,
	$numa       false,
	$mode       flow,    // Rx mode required by the DPDK Flow Rule Manager
	$verbose    99,
	$rules      ./test/fastclick/test_dpdk_nic_rules,        // Better provide absolute path
	$class0     0/000000000000,
	$class1     0/010000000000,
	$class2     0/020000000000,
);

// NIC in Flow Rule Manager's mode
fd0 :: FromDPDKDevice(
	PORT $ifacePCI0, MODE $mode,
	N_QUEUES $queues, NUMA $numa,
	THREADOFFSET 0, MAXTHREADS $threads,
	// FLOW_RULES_FILE $rules,
	VERBOSE $verbose
	// , PAUSE full // according to click docs this is needed for rte_flow but i claim it is entirely unrelated
);

fd0 -> c :: Classifier($class0, $class1, $class2, -);

c[0] -> dropped0 :: AverageCounterIMP()
  	 -> Discard;

c[1] -> dropped1 :: AverageCounterIMP()
  	 -> Discard;

c[2] -> dropped2 :: AverageCounterIMP()
  	 -> Discard;

c[3] -> dropped3 :: AverageCounterIMP()
  	 -> Discard;

DriverManager(
	pause,
	print "",
	print "Dropped0: "$(dropped0.count),
	print "Dropped1: "$(dropped1.count),
	print "Dropped2: "$(dropped2.count),
	print "Dropped3: "$(dropped3.count),
	stop
);
