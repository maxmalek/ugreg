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
so you can simply run them from there or zip up the entire directory and deploy it to a target machine -- the dependencies are all there.



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

## Maiden

*maiden* is a search accelerator for matrix homeservers, and intended to eventually replace [ma1sd](https://github.com/ma1uta/ma1sd) as an identity server.

Right now only LDAP is supported out-of-the-box but any other backend can be added easily;
all that's needed to populate the internal database is a script that outputs JSON
(LDAP support is implemented in the same way to avoid hard dependecies).

*maiden* is intended to be reverse-proxy-wedged before your public matrix homeserver, so that
- `/_matrix/client/*/user_directory/search` goes to maiden
- Anything else goes to the homeserver

so you'll need a a reverse proxy like nginx in front of your synapse/dendrite/... install.

A reasonable set of configs to start is:

```sh
./maiden m_base.json m_import.json m_search.json
```

You can add

- `m_wellknown.json` if you need quick & dirty hosting of `.well-known/...` -- Needs a valid SSL cert because matrix clients expect HTTPS on this endpoint!
- `m_identity.json` to enable the identity server
  - **WARNING: THIS IS UNFINISHED, DO NOT USE**

Then either change `m_import.json` to configure where your user database comes from, or specify this in a private config.
See `example_ldap.json` for how this is done.


## ugreg

TODO

## extrond

TODO
