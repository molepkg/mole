# mole

Simple source-based package manager written in C.

Packages are built from source using plain recipes, tracked with manifests, and stored as directories. No database, no solver, no complexity.

## Install

```
git clone https://github.com/areofyl/mole
cd mole
sudo ./bootstrap.sh
```

## Usage

```
mole install <name>     build and install a package
mole remove <name>      remove a package
mole upgrade [name]     rebuild one or all packages
mole list               show installed packages
mole info <name>        show package details
mole owns <path>        find which package owns a file
mole search <query>     search available recipes
mole sync               pull latest recipes
mole version            print version
```

## Recipes

A recipe is a plain text file:

```
name=zlib
version=1.3.1
source=https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
sha256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
deps=
build=./configure --prefix={{PREFIX}} && make -j{{JOBS}}
install=make DESTDIR={{DESTDIR}} install
```

Local projects work too:

```
name=hum
version=git
source=local:/home/user/projects/hum
deps=
build=make -j{{JOBS}}
install=make DESTDIR={{DESTDIR}} PREFIX={{PREFIX}} install
```

Recipes live in a warren (just a git repo):

```
warren/
    core/
        zlib/recipe
        curl/recipe
    extra/
        neovim/recipe
```

## How it works

1. Download source (or use a local path)
2. Verify sha256
3. Extract and build
4. Install into a staging directory
5. Copy staged files to the system
6. Record the file manifest in `/var/pkg/installed/<name>/`

Uninstalling reads the manifest and deletes each file. That's it.

## Config

`/etc/pkg/config`:

```
PREFIX=/usr/local
JOBS=4
CFLAGS=-O2 -pipe -march=native
REPO=/path/to/warren
```

## License

ISC
