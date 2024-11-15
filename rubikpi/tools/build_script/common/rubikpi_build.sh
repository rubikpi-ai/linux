#!/bin/sh
# Author: Zhao Hongyang <hongyang.zhao@thundersoft.com>
# Date: 2024-11-15
# Copyright (c) 2024, Thundercomm All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
#       copyright notice, this list of conditions and the following
#       disclaimer in the documentation and/or other materials provided
#       with the distribution.
#     * Neither the name of The Linux Foundation nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
# WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
# ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
# BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
# BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
# OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
# IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

set -e

BOOTIMG_EXTRA_SPACE="512"
BOOTIMG_VOLUME_ID="BOOT"
TOP_DIR=`pwd`

usage() {
	echo "\033[1;37mUsage:\033[0m"
	echo "  bash $0 [options]"
	echo
	echo "\033[1;37mOptions:\033[0m"
	echo "\033[1;37m  -h, --help\033[0m   display this help message"
	echo "\033[1;37m  --dtb_package\033[0m    generate a burnable device tree image"
	echo "\033[1;37m  --image_package\033[0m    generate a burnable kernel image"
	echo
}

build_fat_img() {
	FATSOURCEDIR=$1
	FATIMG=$2

	echo $FATSOURCEDIR $FATIMG

	# Calculate the size required for the final image including the
	# data and filesystem overhead.
	# Sectors: 512 bytes
	# Blocks: 1024 bytes

	# Determine the sector count just for the data
	SECTORS=$(expr $(du --apparent-size -ks ${FATSOURCEDIR} | cut -f 1) \* 2)

	# Account for the filesystem overhead.
	# 32 bytes per dir entry
	DIR_BYTES=$(expr $(find ${FATSOURCEDIR} | tail -n +2 | wc -l) \* 32)
	# 32 bytes for every end-of-directory dir entry
	DIR_BYTES=$(expr $DIR_BYTES + $(expr $(find ${FATSOURCEDIR} -type d | tail -n +2 | wc -l) \* 32))
	# 4 bytes per FAT entry per sector of data
	FAT_BYTES=$(expr $SECTORS \* 4)
	# 4 bytes per FAT entry per end-of-cluster list
	FAT_BYTES=$(expr $FAT_BYTES + $(expr $(find ${FATSOURCEDIR} -type d | tail -n +2 | wc -l) \* 4))

	# Use a ceiling function to determine FS overhead in sectors
	DIR_SECTORS=$(expr $(expr $DIR_BYTES + 511) / 512)
	# There are two FATs on the image
	FAT_SECTORS=$(expr $(expr $(expr $FAT_BYTES + 511) / 512) \* 2)
	SECTORS=$(expr $SECTORS + $(expr $DIR_SECTORS + $FAT_SECTORS))

	# Determine the final size in blocks accounting for some padding
	BLOCKS=$(expr $(expr $SECTORS / 2) + ${BOOTIMG_EXTRA_SPACE})

	# mkfs.vfat will sometimes use FAT16 when it is not appropriate,
	# resulting in a boot failure. Use FAT32 for images larger
	# than 512MB, otherwise let mkfs.vfat decide.
	if [ $(expr $BLOCKS / 1024) -gt 512 ]; then
		FATSIZE="-F 32"
	fi

	# Delete any previous image.
	if [ -e ${FATIMG} ]; then
		rm ${FATIMG}
	fi

	mkfs.vfat ${FATSIZE} -n ${BOOTIMG_VOLUME_ID} ${MKFSVFAT_EXTRAOPTS} -C ${FATIMG} ${BLOCKS}

	# Copy FATSOURCEDIR recursively into the image file directly
	mcopy -i ${FATIMG} -s ${FATSOURCEDIR}/* ::/
}



do_dtb_package()
{
	if [ ! -d "$TOP_DIR/rubikpi/tools/pack/dtb_temp/dtb" ]; then
		mkdir -p $TOP_DIR/rubikpi/tools/pack/dtb_temp/dtb
	fi

	if [ ! -d "$TOP_DIR/rubikpi/output/pack" ]; then
		mkdir -p $TOP_DIR/rubikpi/output/pack
	fi

	cp $TOP_DIR/arch/arm64/boot/dts/qcom/rubikpi3.dtb $TOP_DIR/rubikpi/tools/pack/dtb_temp/dtb/combined-dtb.dtb

	build_fat_img $TOP_DIR/rubikpi/tools/pack/dtb_temp/dtb $TOP_DIR/rubikpi/output/pack/dtb.bin

	rm $TOP_DIR/rubikpi/tools/pack/dtb_temp -r
}

DEFAULT_CMD_LINE="root=/dev/disk/by-partlabel/system rw rootwait console=ttyMSM0,115200n8 earlycon pcie_pme=nomsi kernel.sched_pelt_multiplier=4 rcupdate.rcu_expedited=1 rcu_nocbs=0-7 kpti=off kasan=off kasan.stacktrace=off no-steal-acc page_owner=on swiotlb=128"

do_image_package()
{
	if [ ! -d "$TOP_DIR/rubikpi/tools/pack/image_temp" ]; then
		mkdir -p $TOP_DIR/rubikpi/tools/pack/image_temp
	fi

	if [ ! -d "$TOP_DIR/rubikpi/tools/pack/image_temp/mnt" ]; then
		mkdir -p $TOP_DIR/rubikpi/tools/pack/image_temp/mnt
	fi

	if [ ! -d "$TOP_DIR/rubikpi/output/pack" ]; then
		mkdir -p $TOP_DIR/rubikpi/output/pack
	fi

	cp $TOP_DIR/arch/arm64/boot/Image $TOP_DIR/rubikpi/tools/pack/image_temp

	python3.10 $TOP_DIR/rubikpi/tools/pack/ukify build \
		--efi-arch=aa64 \
		--stub=$TOP_DIR/rubikpi/tools/pack/linuxaa64.efi.stub \
		--linux=$TOP_DIR/rubikpi/tools/pack/image_temp/Image \
		--initrd=$TOP_DIR/rubikpi/tools/pack/initramfs-qcom-image-qcm6490.cpio.gz \
		--cmdline="$DEFAULT_CMD_LINE" \
		--uname=6.6.28 \
		--output=$TOP_DIR/rubikpi/tools/pack/image_temp/uki.efi

	cp $TOP_DIR/rubikpi/tools/pack/efi.bin $TOP_DIR/rubikpi/tools/pack/image_temp
	sudo mount $TOP_DIR/rubikpi/tools/pack/image_temp/efi.bin $TOP_DIR/rubikpi/tools/pack/image_temp/mnt

	sudo cp $TOP_DIR/rubikpi/tools/pack/image_temp/uki.efi $TOP_DIR/rubikpi/tools/pack/image_temp/mnt/EFI/Linux
	sudo umount $TOP_DIR/rubikpi/tools/pack/image_temp/mnt
	cp $TOP_DIR/rubikpi/tools/pack/image_temp/efi.bin $TOP_DIR/rubikpi/output/pack

	rm $TOP_DIR/rubikpi/tools/pack/image_temp -r
}

# ========================== Start ========================================
while true; do
	case "$1" in
		-h|--help)          usage; exit 0;;
		--dtb_package)      do_dtb_package ;;
		--image_package)    do_image_package ;;
	esac
	shift

	if [ "$1" = "" ]; then
		break
	fi
done
