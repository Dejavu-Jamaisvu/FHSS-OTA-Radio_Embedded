#!/bin/sh
set -e

modprobe libcomposite
modprobe usb_f_acm

# configfs
mountpoint -q /sys/kernel/config || \
    mount -t configfs none /sys/kernel/config

G=/sys/kernel/config/usb_gadget/radio_gadget

mkdir -p $G
cd $G

# USB 정보
echo 0x1d6b > idVendor
echo 0x0104 > idProduct

mkdir -p strings/0x409
echo "RadioGateway001" > strings/0x409/serialnumber
echo "Radio Project" > strings/0x409/manufacturer
echo "Radio USB Console" > strings/0x409/product

# Configuration
mkdir -p configs/c.1/strings/0x409
echo "USB Serial Console" > configs/c.1/strings/0x409/configuration

# CDC ACM 생성 → /dev/ttyGS0
mkdir -p functions/acm.usb0
ln -s functions/acm.usb0 configs/c.1/

# USB controller 연결
UDC=$(ls /sys/class/udc | head -n 1)

if [ -n "$UDC" ]; then
    echo "$UDC" > UDC
fi
