/**
 */

/**
 * Deploy as follows:
 * sudo ../../bin/click --dpdk -l 0-4 -w 0000:11:00.0 -v -- dpdk-flow-parser.click
 */

define(
	// $ifacePCI0  0000:41:00.0,
	$if         enp81n0s0,
	$queues     1,
	$threads    $queues,
	$numa       false,
	$mode       flow,    // Rx mode required by the DPDK Flow Rule Manager
	$verbose    99,
	$rules      ./test/fastclick/test_dpdk_nic_rules,        // Better provide absolute path
	$class0     0/000000000000,
	$class1     0/010000000000,
	$class2     0/020000000000,
);


fd0::FromDevice($if, PROMISC true)

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
