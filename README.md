# Flash-Level Asynchronous Execution for In-Storage LSM-Tree Operations (FLAX)
This repository contains the source code and benchmarks needed to reproduce the key results in the FLAX paper.

FLAX is built on top of [NVMeVirt](https://github.com/snu-csl/nvmevirt/) and uses [ForestDB-Bench](https://github.com/ForestDB-KVStore/forestdb-bench) for evaluation.

FLAX consists of four major components:
- **CSD-Virt** — A standard-compliant NVMe CSD emulator that supports FLAX
- **Modified Linux kernel** (version 6.0.10)
- **Modified RocksDB** with compaction and GET offloading
- **Modified ForestDB-Bench** for running benchmarks

## System Requirements
FLAX requires three hardware features:
1. **At least six CPU cores**\
Your system must have at least six CPU cores for FLAX to function. In our evaluation setup, we used a total of 35 cores: 9 for NVMeVirt functions, 8 for NAND→SLM data transfer, 2 for CSD management, and 16 to emulate a 16-core ARM processor. We ran FLAX in a NUMA environment to avoid cross-interference. Specifically, NUMA node 0 was used for applications, while NUMA node 1 was dedicated to FLAX.

2. **CPU frequency scaling**\
FLAX's compute cores rely on CPU frequency scaling to emulate the ARM processor. First, determine which CPUs belong to which NUMA node (for example, with `numactl --hardware` or `lscpu`) and choose the cores you will use as compute cores. Then edit `common/perf.sh` to apply frequency scaling to those cores. For instance, in our setup, cores 200–215 are set to 1 GHz.

3. **Sufficient memory**\
Like NVMeVirt, FLAX requires a sufficient amount of host memory reserved for backend storage. In our evaluation, we reserved 350 GB of host memory and used 4 GB of it as SLM (compute memory).

### Tested Environment
- 2 x AMD EPYC 9754 128-Core Processor
- 768 GB DRAM
- Ubuntu 24.04.3 LTS
- Linux kernel version: 6.0.10

## Installation

### 1. Reserving physical memory
Part of the main memory must be reserved as storage for the emulated NVMe device. To reserve a chunk of physical memory, add the following option to `GRUB_CMDLINE_LINUX` in `/etc/default/grub`.
```
GRUB_CMDLINE_LINUX="memmap=350G\\\$400G"
```
This reserves a 350 GB chunk of physical memory (out of 768 GB total) starting at a 400 GB offset. Adjust these values to match your available physical memory and the desired storage capacity.

After editing `/etc/default/grub`, update GRUB and reboot.

```
$ sudo update-grub
$ sudo reboot
```

### 2. Clone the repository
```
$ git clone https://github.com/jmattshim/FLAX
$ cd FLAX
$ git submodule update --init
```

### 3. Build the linux kernel
```
$ cd linux
$ sudo ./build_and_install.sh
```

### 4. Build RocksDB
```
$ cd rocksdb
$ mkdir build & cd build
$ cmake -DCMAKE_BUILD_TYPE=Release -DROCKSDB_BUILD_SHARED=ON ../
$ make
```

### 5. Build ForestDB-Bench
```
$ cd forestdb-bench
$ cmake -DCMAKE_INCLUDE_PATH=/path/rocksdb/include -DCMAKE_LIBRARY_PATH=/path/rocksdb/build ../
$ make
```

### 6. Build and load FLAX
First, edit the `init_nvmev.sh` script for your environment. Below is the configuration used in our evaluation: reserved memory starts at a 400 GB offset, with 350 GB reserved in total and an SLM size of 4 GB.
```
sudo insmod nvmev.ko \
	memmap_start=400 memmap_size=358400 slm_size=4096 \
	cpus=128,131,134,137,140,143,146,149,152 \
	slm_cpus=155,158,161,164,167,170,173,176 \
	csd_cpus=182,185,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215
```
Then edit the `mount.sh` script to set the directory to mount.

Finally, build and load FLAX.
```
$ cd src
$ make
$ ./init_nvmev.sh
```

## Evaluations
