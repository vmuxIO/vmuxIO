# vmuxIO

## Develop

example for ryan.dse.in.tum.de:

load development shell with necessary packages

```shell
nix develop
```

build a few sofware packages and symlink the results

```shell
just build
```

run checks and bind correct dpdk/passthrough drivers

```shell
just prepare ./hosts/ryan.yaml 
```

do a performance measurement with moongen-lachnit:

```shell
sudo ./mgln/bin/MoonGen ./mgln/bin/examples/l2-load-latency.lua 0 1 --rate 100000
```
