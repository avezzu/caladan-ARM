# Caladan

Caladan is a system that enables servers in datacenters to
simultaneously provide low tail latency and high CPU efficiency, by
rapidly reallocating cores across applications. This version is a version specified for ARM processors. Not all functions of the original versions are supported.

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

4) Build the scheduler (IOKernel), the Caladan runtime, and Ksched and perform some machine setup.
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

5) Run Caladan with the following command. In case Caladan is not started on the Bluefield-2, the NIC-PCI address and the NUMA node may have to be adjusted.

```
./start_caladan.sh
```

## How to build RDMA application for Host
```
gcc rdma_host.c -o rdma_host -libverbs
```

