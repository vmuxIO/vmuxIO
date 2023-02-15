# autotest

This is vmuxIO's subproject for automated testing (autotest).

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
