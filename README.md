# Caladan for ARM Architecture

Caladan is a system that enables servers in datacenters to
simultaneously provide low tail latency and high CPU efficiency, by
rapidly reallocating cores across applications. This version is a version specified for ARM processors. Not all functions of the original versions are supported.

## Setup for Bluefield-2

To run Caladan on the Bluefield-2, you need to configure tje Bluefield-2 in Separated Host Mode. Instructions on how to do this can be found [here](https://docs.nvidia.com/networking/display/BlueFieldSWv35111601/Modes+of+Operation). Depending on the configuration of the server and client, you may also need to adjust the MTU of the SmartNIC.

## How to Run Caladan

1) Clone the Caladan repository.

2) Install dependencies.

```
sudo apt install make gcc cmake pkg-config libnl-3-dev libnl-route-3-dev libnuma-dev uuid-dev libssl-dev libaio-dev libcunit1-dev libclang-dev libncurses-dev meson python3-pyelftools
```

3) Set up submodules (e.g., DPDK, SPDK, and rdma-core).

```
make submodules
```

4) Modify line 33 in the [Makefile](https://github.com/avezzu/caladan-aarch64/blob/main/Makefile) to the folder of the application you want to compile. For example, to build the UDP applications, you need to set:
```
test_src = $(wildcard caladan-bmarks/UDP/*.c)
```

5) Build the scheduler (IOKernel), the Caladan runtime, and Ksched and perform some machine setup.
Before building, set the parameters in build/config (e.g., `CONFIG_SPDK=y` to use
storage, `CONFIG_DIRECTPATH=y` to use directpath, and the MLX4 or MLX5 flags to use
MLX4 or MLX5 NICs, respectively, ). To enable debugging, set `CONFIG_DEBUG=y` before building.
```
make clean && make
pushd ksched
make clean && make
popd
sudo ./scripts/setup_machine.sh
```

6) Run Caladan with the following command. Depending on your hardware setup, the NIC-PCI address and the NUMA node may have to be adjusted.

```
./start_caladan.sh
```

## How to build RDMA application for Host
```
gcc rdma_host.c -o rdma_host -libverbs
```

## How to run benchmarks

A default server and client configuration can already be found in the config files, which were also used for the benchmarks. Depending on the use case, you can adapt them accordingly.

Run the server and client process:
```
sudo ./<application> <param>
```
For example for the server

```
sudo ./caladan-bmarks/UDP/aggregate server.config server 1000 1 1
```
and for the client

```
sudo ./caladan-bmarks/UDP/aggregate client.config client 1000 1 1
```

Run host RDMA process (when using RDMA, always start this process first):
```
./rdma_host <#threads>
```

## Dealing with Failed Attempts
The ported version sometimes encounters issues with applications that have excessive multithreaded overhead. If the overhead becomes too large, certain assertions in Caladan may fail. You can retry until a successful attempt or alternatively, you can change the optimization flag and recompile the project. To do this, modify line 45 in [shared.mk](https://github.com/avezzu/caladan-aarch64/blob/main/build/shared.mk) to:
```
FLAGS += -DNDEBUG -O0
```


