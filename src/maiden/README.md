## maiden

*maiden* is a search accelerator for matrix homeservers, and intended to eventually replace [ma1sd](https://github.com/ma1uta/ma1sd) as an identity server.

Right now only LDAP is supported out-of-the-box but any other backend can be added easily;
all that's needed to populate the internal database is a script that outputs JSON
(LDAP support is implemented in the same way to avoid hard dependecies).

*maiden* is intended to be reverse-proxy-wedged before your public matrix homeserver, so that
- `/_matrix/client/*/user_directory/search` goes to maiden
- Anything else goes to the homeserver

so you'll need a a reverse proxy like nginx or Traefik in front of your synapse/dendrite/... install. See below.


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

---

Example nginx config snippet for reverse proxying:
```
location ~ /_matrix/client/[0-9a-z]+/user_directory/search {
    resolver 127.0.0.1 valid=5s;
    # where maiden is listening
    set $backend "127.0.0.1:8088";
    proxy_pass http://$backend;

    proxy_set_header Host $host;
    proxy_set_header X-Forwarded-For $remote_addr;
}
```

Note that depending on the version of the matrix spec, the regex will match *r0* or *v3*. Maiden handles both.

