#!/bin/bash
#
# qemu-test.sh -- the OI-Apple media tests in QEMU/KVM with OVMF (UEFI),
# notes/oi-apple-installer-eval.md step 8.  Runs on a Linux host.
# Each run gets a monitor socket (screendump, sendkey) and a VNC display.
#
#   qemu-test.sh live    ISO                 boot the medium only
#   qemu-test.sh install ISO WORKDIR         the medium as a USB stick on
#                                            xhci port 1, targets: a 4Kn
#                                            NVMe (32 GB) and a USB disk
#                                            (16 GB) on xhci port 3
#   qemu-test.sh nvme    WORKDIR             boot the installed NVMe alone
#   qemu-test.sh usb     WORKDIR             boot the installed USB disk
#                                            alone, on another controller
#                                            and port (EHCI port 2): bug A;
#                                            then check rpool (bug B)
#
# WORKDIR holds nvme.img, usb.img and OVMF_VARS.fd (made on first use).
# VNC :N with N = $VNC (default 5).  Monitor: WORKDIR/mon.sock.
set -e
MODE=$1; shift
OVMF=/usr/share/OVMF/OVMF_CODE_4M.fd
VNC=${VNC:-5}
# NIC model: e1000e (82574, MSI; e1000g).  QEMU's e1000 (82540) needs
# legacy INTx, whose ACPI routing (_SRS) fails on q35+OVMF: no link.
NIC=${NIC:-e1000e}
Q=(qemu-system-x86_64 -machine q35,accel=kvm -cpu host -smp 4 -m 8192
   -drive if=pflash,format=raw,readonly=on,file=$OVMF
   -vga std -display vnc=127.0.0.1:$VNC
   -nic user,model=$NIC)

work() {
	W=$1; mkdir -p "$W"
	[ -f "$W/OVMF_VARS.fd" ] || cp /usr/share/OVMF/OVMF_VARS_4M.fd "$W/OVMF_VARS.fd"
	[ -f "$W/nvme.img" ] || truncate -s 32G "$W/nvme.img"
	[ -f "$W/usb.img" ] || truncate -s 16G "$W/usb.img"
	Q+=(-drive if=pflash,format=raw,file=$W/OVMF_VARS.fd
	    -monitor unix:$W/mon.sock,server,nowait -serial file:$W/serial-$MODE.log)
}

case $MODE in
live)
	ISO=$1; W=$(mktemp -d); work "$W"
	Q+=(-device qemu-xhci,id=xhci
	    -drive if=none,id=stick,format=raw,readonly=on,file=$ISO
	    -device usb-storage,bus=xhci.0,port=1,drive=stick,removable=on) ;;
install)
	ISO=$1; work "$2"
	Q+=(-device qemu-xhci,id=xhci,p2=4,p3=4
	    -drive if=none,id=stick,format=raw,readonly=on,file=$ISO
	    -device usb-storage,bus=xhci.0,port=1,drive=stick,removable=on
	    -drive if=none,id=usbdisk,format=raw,file=$W/usb.img
	    -device usb-storage,bus=xhci.0,port=3,drive=usbdisk
	    -drive if=none,id=nvme0,format=raw,file=$W/nvme.img
	    -device nvme,drive=nvme0,serial=OIAPPLE4KN,logical_block_size=4096,physical_block_size=4096) ;;
nvme)
	# bootindex: OVMF_VARS keeps the install run's NVMe boot option
	# (another PCI slot, the install run has xhci too) and makes no new
	# one: without it OVMF goes to PXE (v10d).  installboot also puts
	# EFI/BOOT/BOOTX64.EFI on the ESP, the path a Mac's firmware uses.
	work "$1"
	Q+=(-drive if=none,id=nvme0,format=raw,file=$W/nvme.img
	    -device nvme,drive=nvme0,serial=OIAPPLE4KN,logical_block_size=4096,physical_block_size=4096,bootindex=1) ;;
usb)
	work "$1"
	Q+=(-device usb-ehci,id=ehci
	    -drive if=none,id=usbdisk,format=raw,file=$W/usb.img
	    -device usb-storage,bus=ehci.0,port=2,drive=usbdisk,bootindex=1) ;;
*)
	sed -n '3,20p' "$0"; exit 2 ;;
esac
echo "VNC 127.0.0.1:$((5900 + VNC)), monitor $W/mon.sock"
exec "${Q[@]}"
