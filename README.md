# mypkg — a rude-ass package manager for maniacs

mypkg is a dirty, no-bullshit, minimalistic package manager written in C, for those who like it raw, fast, and with a pinch of profanity.

## Features

- Installs .tar.xz packages from a remote repo (https://loxsete.github.io/mypcg-repo/)
- Dependency checking with aggressive output
- Keeps a record of installed packages
- Removes packages and all their crap
- Logs installed files
- No systemd, no nonsense

## Requirements

- Linux
- libarchive (install via package manager)

## Build
``` bash
make
```
## Usage
``` bash
cd /
mypkg install <package>   # install some shit
mypkg remove <package>    # nuke that package
mypkg list                # show installed crap
mypkg info <package>      # get package details
```
## Install paths
/var/db/mypkg — install db
/var/cache/mypkg — downloaded archives
/.usr/local/bin/mypkg — the binary
## PKGINFO example
``` bash
name=neovim
version=0.9.5
arch=x86_64
description=Kick-ass text editor for hackers
depends=libtermkey,libvterm
size=1536000
```
## Disclaimer
**This thing is raw and stupid. It doesn’t check signatures, it may break stuff, and it sure as hell isn’t secure. Don’t cry if it eats your system.*

## License
**WTFPL / Public Domain. Do whatever you want.**
