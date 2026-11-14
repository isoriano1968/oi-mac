#!/bin/sh
# Stage 1 of first-boot-test.md: read-only facts from the Mac Pro before
# amdgpu is installed. Changes nothing. Run as root (prtconf -v and mdb -k
# need it), e.g.  pfexec sh macpro-stage1-check.sh > stage1.txt 2>&1
echo "== system"; uname -a; cat /etc/release | head -1; beadm list
echo "== display nodes (driver, path)"
prtconf -D | grep -i -E "display|vgatext"
echo "== display nodes with PCI ids, compatible, interrupts (prtconf -v)"
prtconf -v 2>/dev/null | nawk '
	/^[ \t]*display, instance/ { show = 1; n = 0; print; next }
	show && /^[ \t]*[a-z].*, instance/ { show = 0 }
	show && /name=.(compatible|vendor-id|device-id|subsystem-id|subsystem-vendor-id|revision-id|reg|assigned-addresses|interrupts|pcie-link-.*)./ { print; getline; print }
'
echo "== device paths of the GPUs (/devices)"
ls -d /devices/pci@0,0/display@* /devices/pci@0,0/*/display@* \
    /devices/pci@0,0/*/*/display@* 2>/dev/null | grep -v ":"
echo "== loaded graphics modules (gfx-drm's misc/drm must not be loaded)"
modinfo | grep -i -E " (drm|drm_core|linuxkpi|vgatext|agpgart|agptarget|i915|radeon|amdgpu) "
echo "== driver aliases mentioning 1002 (AMD)"
grep -i "1002" /etc/driver_aliases
echo "== crash dumps"; dumpadm; df -h /var/crash 2>/dev/null || df -h /var
echo "== root pool"; zpool list; df -h /
echo "== IOMMU (immu): 1 = on"
echo "immu_enable/D" | mdb -k 2>/dev/null
echo "== interrupts and MSI of the GPUs (vectors in use)"
echo "::interrupts -d" | mdb -k 2>/dev/null | grep -i -E "vgatext|display|amdgpu" | head
echo "== network"; ipadm show-addr 2>/dev/null; dladm show-phys 2>/dev/null
echo "== done"
