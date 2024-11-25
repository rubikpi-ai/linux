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

TOP_DIR=`pwd`

usage() {
	echo "\033[1;37mUsage:\033[0m"
	echo "  bash $0 [options]"
	echo
	echo "\033[1;37mOptions:\033[0m"
	echo "\033[1;37m  -h, --help\033[0m   display this help message"
	echo "\033[1;37m  -r, --reboot\033[0m  exit fastboot mode"
	echo "\033[1;37m  -d, --dtb\033[0m    flash device tree image"
	echo "\033[1;37m  -i, --image\033[0m    flash kernel image"
	echo
}

do_dtb_flash() {
	if [ ! -f "$TOP_DIR/rubikpi/output/pack/dtb.bin" ]; then
		echo "No dtb.bin file found, please use rubikpi-build. sh to generate"
		exit
	fi

	fastboot flash dtb_a $TOP_DIR/rubikpi/output/pack/dtb.bin
}

do_image_flash() {
	if [ ! -f "$TOP_DIR/rubikpi/output/pack/efi.bin" ]; then
		echo "No efi.bin file found, please use rubikpi-build. sh to generate"
		exit
	fi

	fastboot flash efi $TOP_DIR/rubikpi/output/pack/efi.bin
}

do_fastboot_reboot() {
	fastboot reboot
}

# ========================== Start ========================================
while true; do
	case "$1" in
		-h|--help)           usage; exit 0;;
		-d|--dtb)            do_dtb_flash ;;
		-i|--image)          do_image_flash ;;
		-r|--reboot)         do_fastboot_reboot ;;
	esac
	shift

	echo $1

	if [ "$1" = "" ]; then
		break
	fi
done
