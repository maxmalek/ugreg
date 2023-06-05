# What's in this repo?

- [maiden](src/maiden) - A search accelerator for matrix homeservers
- [extrond](src/extrond) - Provides a REST interface to control [Extron SMP 351](https://www.extron.com/product/smp351) machines over the network.
  Can be extended to support other devices via telnet or similar.


Unfinished, do not use:

- [ugreg](src/ugreg) - A JSON aggregator, "database" and cache intended for use in front of slow backends


All projects use the same underlying shared code so they are bunched together in one repo, sorry.

# Building

Everything in this repo is developed in Visual Studio on Windows and deployed on Linux.
It's fairly portable code so other OSes should work too, but this is untested.
The code itself is mostly C++'03 but uses some C++'17 STL things so C++'17 is needed to build it.


Starting in the repo root, build like any other cmake project:

```sh
mkdir build
cd build
cmake ..
make -j8
```

By default, only *maiden* and its dependencies are built.
There are no external hard dependencies.

You probably want SSL support but it can do without it if you don't need it.
If in doubt, install `openssl-dev` or get the headers yourself and point cmake to them, the rest should be auto-detected.


Optionally, if you want to configure things, instead of `cmake ..` use:

```sh
ccmake ..
```

or

```sh
cmake-gui ..
```

and change settings as you like.

Upon building, binaries are copied to their respective dirs under `bin/*`,
so you can simply run them from there or zip up the respective directory and deploy it to a target machine -- data files and dependencies are all there.

If you're on windows, you may have to put some [OpenSSL DLLs](https://slproweb.com/products/Win32OpenSSL.html) alongside the executables.


# Running

#### First things first

All binaries take as commandline parameters a list of config files to apply.
A set of default configs is present in each `bin/<project>` subdir so it's enough to do eg.
```sh
./maiden *.json
```
to apply all json files in alphabetical order.
Note that config files are applied in order, and duplicate entries are merged. That means:

* If both old and new entry are maps (`{"key":"value", ...}`), the new map is merged over the old one, recursively
* If both old and new entry are arrays (`[...]`), that array is appended
* Other cases simply get replaced

For example, if this is the first config:

```json
{
    "homeserver": { "host":"localhost", "port":8448 },
    "listen": [ 80 ],
    "extra": { "foo": 42 },
}
```

and this is the second one that gets applied after the first:

```json
{
    "save": true,
    "homeserver": { "port":1337, "ssl": true },
    "listen": [ 8080 ],
    "extra": none,
}
```

then we end up with this config in memory:

```json
{
    "save": true,
    "homeserver": { "host":"localhost", "port":1337, "ssl":true },
    "listen": [ 80, 8080 ],
    "extra": none,
}
```

The json parsing is relaxed so it accepts extra commas at the end of lists,
C++-style comments (`// ...`), and C-style block comments (`/* ... */`).

With this merging in place you can load only the configs you need, and also split your config into a public part that is fine push into a public repo,
and another private config with passwords and stuff that you can protect via ansible-vault or similar.

Each project expects to be started in its working directory (`bin/<project>/`) in its default config, in case it uses external files like scripts and such.

## Common switches

- `-v` for verbose logging, each added `v` makes it more verbose
- `--` to end processing switches


# Sub-projects

### Maiden

See [maiden](src/maiden/README.md).


### extrond

See [extrond](src/extrond/README.md).

### ugreg

Unfinished. Do not use.

See [ugreg](src/ugreg/README.md).

----------------------------------------------------------


## Library dependencies

### Included

For reference. All necessary files are in the repo, but may need to be updated occasionally in case bugs or compatibility problems surface:

- [civetweb](https://civetweb.github.io/civetweb/) - The embedded web server
- [Lua](https://www.lua.org/) - Embedded scripting language
- [rapidjson](https://rapidjson.org/) - JSON parser
- [brotli](https://github.com/google/brotli) - Compression
- [miniz](https://github.com/richgel999/miniz/) - Compression
- [zstd](https://github.com/facebook/zstd) - Compression
- A subset of [TomCrypt](https://www.libtom.net/LibTomCrypt/) for hash functions and base64 de-/encoding
- [subprocess.h](https://github.com/sheredom/subprocess.h) to spawn processes
- [upgrade_mutex](https://github.com/HowardHinnant/upgrade_mutex) because even C++17 does not have such a thing
- Bits and pieces from:
  - [mattiasgustavsson/libs](https://github.com/mattiasgustavsson/libs): **strpool.h** efficient string storage to reduce memory and enable O(1) string comparisons
  - [lib_fts](https://github.com/forrestthewoods/lib_fts): **fts_fuzzy_match.h for** fuzzy search
  - [safe_numerics.h](https://src.chromium.org/viewvc/chrome/trunk/src/base/safe_numerics.h?revision=177264&pathrev=177264) originally from the Chromium source code
  - The sponge function from the [ascon](https://github.com/ascon/ascon-c) family of hash function, privately shared by [github.com/Ferdi265](https://github.com/Ferdi265). Used as RNG.
  - [tinypile](https://github.com/fgenesis/tinypile/tree/wip): Lua allocator, portable malloca(), UTF-8 casefolding data

### External libs

- For civetweb:
  - [OpenSSL](https://openssl.org)
  - Alternatively [mbedTLS](https://github.com/Mbed-TLS/mbedtls), but support for this is not as good
