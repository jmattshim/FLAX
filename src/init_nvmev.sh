#!/bin/bash

# init_nvmev.sh script at star1

# if [ ! -f "nvmev.ko" ]; then
# 	echo "NVMeVirt module does not exist at $1"
# 	exit
# fi

#echo "Initialize CPUs..."
#$PWD/setup_cpus.sh

#echo "Load nvme module"
#sudo modprobe nvme
sudo umount /mnt/nvme
sudo rmmod nvmev
sudo nvme list
make clean || exit
make -j8 || exit

echo "Load NVMeVirt kernel module..."
sudo insmod nvmev.ko \
	memmap_start=400 memmap_size=358400 slm_size=4096 \
	cpus=128,131,134,137,140,143,146,149,152 \
	slm_cpus=155,158,161,164,167,170,173,176 \
	csd_cpus=182,185,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215


echo "Set CPU Frequency"
source perf.sh

sleep 3
./mount.sh

cd lib
./build.sh
