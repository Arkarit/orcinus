<!--
SPDX-FileCopyrightText: 2026 Orcinus contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Linux Problem Knowledge Base

Typical Linux desktop problems (mostly "newcomer" issues on KDE/Ubuntu-like systems)
with diagnosis and fix paths. Collected from real admin sessions; intended as source
material for the AI support / system repair wizards in Orcinus.

## Rules

- One problem per file in `entries/`, named `NNNN-short-slug.md`.
- Generic: no personal data, user names, private paths or machine IDs.
  Machine-specific history belongs in the admin log of the system where it happened, not here.
- Diagnosis steps must be read-only. Every fix needs a revert path.
- Mark what needs root and what is risky.

## Entry template

```markdown
---
id: NNNN
title: <short problem title>
tags: [storage, performance, ...]
applies_to: <distros / kernels / hardware where relevant>
requires_root: yes|no
risk: low|medium|high
status: verified|draft      # verified = fix tested on a real system
---

<!--
SPDX-FileCopyrightText: 2026 Orcinus contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

## Symptoms
How users describe it, in their own words.

## Diagnosis
Read-only commands, with what to look for.

## Cause

## Fix
### Quick / temporary
### Permanent

## Risks and revert

## Wizard notes
What an AI wizard should check, ask, and confirm; when NOT to apply the fix.

## References (optional)
```

## Index

| ID | Title | Tags | Status |
|---|---|---|---|
| [0001](entries/0001-desktop-freezes-during-large-copies.md) | Desktop freezes during large file copies | storage, performance, io | verified |
| [0002](entries/0002-relocate-libvirt-vm-images.md) | Moving libvirt/KVM VM images to another disk without breaking snapshots | virtualization, libvirt, storage | verified |
