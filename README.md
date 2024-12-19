# AUVFS

Atomic Upgrade over Virtual Filesystem

## TLDR: Usage
```
git submodule update --init --recursive
make
./auvfs pacman -Syu
```

## Why this matters?
- Common Linux package managers are not **ATOMIC**
- Non-atomic upgrades can render the system ***unusable***


## What do we want?
- An **ATOMIC** upgrade
- Users observe:
    - Either before the any package upgrade happens
    - Either the upgrade fully completed
    - But never a partially upgraded system


## Why not XXX?
- Why not OSTree?
  - `rpm-ostree` is great, but I'm using Arch :-(
- Why not `btrfs` / `zfs`?
  - They are great too. But I'm using `ext4` on my root partition :-(


## How it works?
- It is divided to 3-phases:
    - Apply: build an full and working out-of-root tree, using hardlinks and symlinks
    - Commit: use `rename` to commit, as it is (almost) atomic.
    - Finalize: fix remaining symlinks
- As long as the Commit phase is atomic, your system won't brick.
- Q: That's still not atomic, you commit `/usr` etc. separately!
  - A: You're right. If we can get everything under `/usr`, it will work.
