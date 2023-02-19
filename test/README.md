# autotest

This is vmuxIO's subproject for automated testing (autotest).

## Overview

- host: where you run `autotest`
- guest: runs on the host
- loadgen: anything

## Before You Use It

- SSH keys and config should be in place for password-less connection to the
  host, guest and loadgen machines ... Note: autotest only allows you to set
  the hostname for each, so everything like, like an SSH port different from
  22 or else, should be configured in your SSH config.
- Create your own `autotest.cfg` under `conf/` and symlink it to 
  `autotest.cfg`. This is the general configuration for autotest, which
  mostly contains settings for the three machines. An example with comments
  about every variable can be found under `conf/autotest_example.cfg`.
- Create your own `tests.cfg` under `conf/` and symlink it to 
  `tests.cfg`. This is the configuration of the tests. An example with
  comments about every variable can be found under `conf/tests_example.cfg`.
- Make sure all the directories set in your config files exist on the
  respective machines.
- **Optional**: A clone of the `vmuxIO/dat` repo (https://github.com/vmuxIO/dat) in
  the location set as output directory in your `tests.cfg` so you can directly
  commit and push your data to our data repo.
- Make sure MoonGen, the XDP reflector and QEMU are built.
- **Optional**: Run the script `scripts/setup-admin-bridge.sh` on the host to setup
  the admin bridge for the guest. This is done by autotest, but can fail
  sometimes, and since the admin bridge is never deleted by autotest, it
  can be helpful to pre-create it.
- **Optional**: Run the script `scripts/create-guest-nat-rules.sh` on the host
  to create iptables NAT rules for guest's internet access and SSH exposure
  via port forwarding. The latter is only necessary if you use autotest
  remotely.

## How To Use It

`autotest` has several commands:

- **ping**: Ping host, loadgen and guest machine.
- **run-guest**: Run the guest on the host.
- **kill-guest**: Kill the guest.
- **setup-network**: Just setup the network for the guest.
- **teardown-network**: Teardown the guest network, this can sometimes help after
  some error.
- **test-load-lat-file**: Run load latency tests defined in the test config file.
- **acc-load-lat-file**: force accumulation of all load latency test repetitions.
- **shell**: Enter a Python3 shell with access to the server objects for debugging.
- **upload-moonprogs**: Upload the MoonGen programs to the servers.

Note that `autotest` and all its commands have a `-h/--help` option to show the
usage. Also note the `-v/--verbose` argument which can be specified zero to 3
times to set the verbosity of the output.
