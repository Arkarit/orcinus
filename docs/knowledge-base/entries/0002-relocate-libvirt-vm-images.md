---
id: 0002
title: Moving libvirt/KVM VM images to another disk without breaking snapshots
tags: [virtualization, libvirt, kvm, qemu, storage, snapshots, apparmor]
applies_to: libvirt/QEMU hosts (virt-manager, GNOME Boxes system mode), esp. Ubuntu/Debian with AppArmor
requires_root: yes
risk: medium
status: verified
---

<!--
SPDX-FileCopyrightText: 2026 Orcinus contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

## Symptoms

- "My system disk is full, the VMs in /var/lib/libvirt/images are huge, I want them on my data disk."
- After moving image files with a file manager: VM does not start, "Cannot access storage file",
  "Permission denied", snapshots missing or "backing file not found".
- Copy of the image folder takes far more space or time than expected.

## Diagnosis

```bash
virsh -c qemu:///system list --all                       # VMs must be shut off
virsh -c qemu:///system pool-list --all --details        # where pools live
virsh -c qemu:///system vol-list default --details       # capacity vs allocation (sparse files)
virsh -c qemu:///system snapshot-list <vm> --tree        # snapshot chains
virsh -c qemu:///system domblklist <vm> --details        # current top image per VM
sudo qemu-img info --backing-chain <top-image>           # absolute backing-file paths
df -h /var/lib/libvirt/images <target-mount>             # free space
```

Indicators: image names like `vm.snapshotname` (external snapshots), backing chains with
absolute paths, capacity much larger than allocation (sparse), AppArmor enabled (`aa-status`).

## Cause

- **External snapshots** chain qcow2 overlays via **absolute backing-file paths**; libvirt snapshot
  XML (`/var/lib/libvirt/qemu/snapshot/`) also stores paths. Moving files breaks both.
- **AppArmor** (virt-aa-helper) builds per-VM profiles from the configured paths; symlinks resolve
  to the real path, which may not be allowed → "Permission denied".
- **Sparse files:** naive copies (file managers, `cp` without `--sparse`) write the full virtual
  size, filling the target and taking much longer.
- Copying on the same physical disk can freeze the desktop – see entry 0001.

## Fix

### Recommended: keep the path, move the data (bind mount)

1. Shut down all VMs. Stop libvirt **including its sockets** (otherwise socket activation restarts it):
   `systemctl stop libvirtd.socket libvirtd-ro.socket libvirtd-admin.socket libvirtd.service`
   (modular daemons: `virtqemud*`, `virtstoraged*` instead).
2. Copy, preserving sparseness, ACLs, xattrs, ownership, at low priority:
   `ionice -c2 -n7 nice -n19 rsync -aHAX --sparse --numeric-ids --info=progress2 /var/lib/libvirt/images/ /mnt/data/libvirt/images/`
3. Verify: `rsync -aHAX --checksum --dry-run --itemize-changes --delete SRC/ DST/` must print nothing.
4. Back up `/etc/fstab`, add:
   `/mnt/data/libvirt/images /var/lib/libvirt/images none bind,nofail,x-systemd.requires-mounts-for=/mnt/data 0 0`
5. Make libvirt wait for the mount (else it silently uses the stale originals underneath):
   `/etc/systemd/system/libvirtd.service.d/10-images-mount.conf`
   ```
   [Unit]
   RequiresMountsFor=/var/lib/libvirt/images
   ```
   `systemctl daemon-reload; mount /var/lib/libvirt/images`
6. Start libvirt, `virsh pool-refresh default`, check snapshot trees, boot each VM.
7. Reboot once to confirm mount order. Only then delete the originals hidden under the mount,
   via a second bind mount of `/` (e.g. `mount --bind / /mnt/rootfs` → `/mnt/rootfs/var/lib/libvirt/images/`).

### Alternative: really change paths

New storage pool + `qemu-img rebase -u -b <new-backing> -F qcow2` on every overlay + editing
domain and snapshot XML. Error-prone with snapshot trees; use only without external snapshots.

## Risks and revert

- Medium: VM data is involved. Never delete originals before verification and a successful boot.
- Revert: `umount /var/lib/libvirt/images`, restore fstab backup, remove drop-in,
  `systemctl daemon-reload`, restart libvirt – originals are still in place.
- `nofail` keeps boot working if the data disk is missing; the drop-in then stops libvirtd from
  starting against the wrong (old or empty) directory.

## Wizard notes

- Check for running VMs and refuse while any is running.
- Detect external snapshots (`snapshot-list`, `qemu-img info --backing-chain`) and prefer the
  bind-mount method automatically when they exist.
- Check target filesystem: ext4/xfs/btrfs fine; **not** NTFS/exFAT (permissions, sparse, locking).
- Check free space against *allocated* size plus margin, and warn that the source disk only
  frees up after the final deletion step.
- Apply entry 0001 (writeback/BFQ) first if source and target share a physical disk.
- Do the deletion as a separate, explicitly confirmed step after reboot + boot test.
- Before deleting, check that every original exists in the copy with the same size. Expect the
  **active top image of each VM** (`domblklist`) to differ after the boot test – the VM wrote to
  the copy. Accept a difference only for those files and only if the copy is newer; anything
  else is a real mismatch → abort.
- Delete only the directory *contents*; the directory itself is the mount point.

## Verified data point

~270 GiB allocated (416 GB apparent, 19 files, external snapshot trees) moved on the same
QLC SATA SSD: copy 41 min (~160 MB/s), checksum verify 21 min, desktop responsive throughout.
After reboot: bind mount came up before libvirtd, all 3 VMs booted fine, snapshot trees intact.
Originals deleted via temporary non-recursive bind mount of `/`; system disk went from 68 % to 40 % used.
