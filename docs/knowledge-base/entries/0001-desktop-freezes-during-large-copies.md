---
id: 0001
title: Desktop freezes during large file copies
tags: [storage, performance, io, writeback, scheduler, ssd]
applies_to: Most distros with default kernel writeback settings; worst with lots of RAM and slow-writing drives (QLC SSDs, SMR HDDs, USB sticks)
requires_root: yes
risk: low
status: verified
---

<!--
SPDX-FileCopyrightText: 2026 Orcinus contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

## Symptoms

- "When I copy big files (VM images, videos, backups) the whole system becomes unusable / laggy / unresponsive."
- Mouse still moves, but apps freeze, windows do not redraw, browser stalls, audio/video stutters.
- Copy progress jumps to a high value quickly, then crawls; or "100 % but the copy dialog does not close".
- "Safely remove" of a USB stick takes minutes after the copy "finished".
- Worse when source and target are on the same physical disk.

## Diagnosis

```bash
# Writeback limits (defaults: dirty_ratio=20, dirty_background_ratio=10, *_bytes=0)
sysctl vm.dirty_ratio vm.dirty_background_ratio vm.dirty_bytes vm.dirty_background_bytes
free -h                                  # RAM size: 20 % of 64 GiB = ~12 GiB dirty data
# During a copy: amount of dirty/writeback data piling up
grep -E '^(Dirty|Writeback):' /proc/meminfo
# I/O scheduler per disk (bracketed = active)
cat /sys/block/*/queue/scheduler
# Drive model: QLC SSD (e.g. "QVO"), SMR HDD, USB stick → slow sustained writes
lsblk -d -o NAME,MODEL,ROTA,TRAN
# Same physical disk for source and target?
findmnt -T <source> -o SOURCE; findmnt -T <target> -o SOURCE
# Optional: live I/O pressure (some > 20-30 % while frozen confirms I/O stall)
cat /proc/pressure/io
```

Indicators: large RAM with ratio-based defaults, `Dirty:` in the GiB range during the copy,
`mq-deadline`/`none` on SATA/USB disks, a slow-writing drive, same-disk copy.

## Cause

Three effects add up:

1. **Oversized dirty page cache.** The kernel buffers writes in RAM up to `dirty_ratio`
   (default 20 % of RAM) before forcing writers to wait. On modern machines this is
   many GiB. When the cache drains, every process that writes anything (browser cache,
   desktop config, logs) is throttled together with the copy → system-wide stalls.
   It also makes progress bars lie: they measure "written to RAM", not "written to disk".
2. **Slow sustained write speed.** QLC SSDs drop to ~80–160 MB/s once their SLC cache
   is full (smaller cache when the drive is fuller); SMR HDDs and USB sticks can be
   even slower. Same-disk copies halve the available bandwidth again (read + write).
3. **No fairness between processes.** `mq-deadline` and `none` do not prioritise
   interactive processes, and `ionice` has no effect with them.

## Fix

### Quick / temporary (no root for own files)

```bash
# Copy with idle/low I/O priority (effective only with BFQ) and a bandwidth cap
ionice -c2 -n7 nice -n19 rsync -a --info=progress2 --bwlimit=120M SRC/ DST/
```

### Permanent (root)

1. Limit dirty memory to a fixed size (overrides the ratio settings):

   `/etc/sysctl.d/90-dirty-writeback.conf`
   ```
   vm.dirty_bytes = 536870912            # 512 MiB
   vm.dirty_background_bytes = 134217728 # 128 MiB
   ```
   Apply: `sysctl -p /etc/sysctl.d/90-dirty-writeback.conf`

2. Use the BFQ scheduler for SATA/USB disks (fair, desktop-friendly, makes `ionice` work):

   `/etc/udev/rules.d/60-ioscheduler.rules`
   ```
   ACTION=="add|change", KERNEL=="sd[a-z]*", ENV{DEVTYPE}=="disk", ATTR{queue/scheduler}="bfq"
   ```
   `/etc/modules-load.d/bfq.conf` containing `bfq` (if BFQ is built as a module).
   Apply: `modprobe bfq; udevadm control --reload; udevadm trigger --subsystem-match=block --action=change`

   Keep NVMe on `none` (very high IOPS; BFQ overhead not worth it there).

Result on a verified system (Ryzen, 62 GiB RAM, Samsung 870 QVO 4 TB, ~330 GB same-disk copy):
before – desktop unusable; after – copy at ~115 MB/s, video playback and desktop fully responsive.

## Risks and revert

- Low risk. Smaller dirty cache can reduce peak throughput of short bursty writes slightly;
  not noticeable on desktop workloads.
- Revert: delete the three files and reboot, or live:
  `sysctl vm.dirty_ratio=20 vm.dirty_background_ratio=10` and
  `echo mq-deadline > /sys/block/sdX/queue/scheduler`.
- Check for conflicting existing settings first (`grep -r dirty /etc/sysctl.conf /etc/sysctl.d/`,
  other udev rules setting `queue/scheduler`, tuned/TLP/gaming tweaks).

## Wizard notes

- Trigger phrases: "freezes when copying", "laggy during transfer", "USB copy never finishes".
- Run the diagnosis read-only and show the user the numbers (RAM, current dirty limit in GiB,
  drive model, scheduler) – the explanation is convincing and educational.
- Ask before changing system files; show the exact files and the revert path.
- Scale the dirty limits: 256–512 MiB background/limit is a good default for desktops;
  do not apply on servers/databases without asking about workload.
- Do not touch NVMe schedulers. Do not apply if the user runs a tuning tool that manages
  these values (tuned, custom gaming kernels) – point it out instead.
- For VM image moves, additionally warn about absolute backing-file paths (see future entry on
  relocating libvirt images) and sparse files (`rsync --sparse`, `cp --sparse=always`).
- After applying: verify with `sysctl` and `cat /sys/block/*/queue/scheduler`, and confirm
  persistence after the next reboot.

## References

- Kernel docs: Documentation/admin-guide/sysctl/vm.rst (dirty_*), Documentation/block/bfq-iosched.rst
