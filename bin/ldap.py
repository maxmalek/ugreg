#!/usr/bin/env python3

import ldap3
import argparse
import sys
import json
import re
from os import environ as env

def eprint(*args, **kwargs):
    print(*args, file=sys.stderr, **kwargs)

host = env["HOST"]
dn = env["DN"]
filtfmt = env["FILTER"]
pwd = env["PASS"]
fields = env["FIELDS"].split()
base = env["BASE"]

#eprint(" ".join(sys.argv))

if len(sys.argv) <= 1:
    exit(1)

# this is the startup check. Make sure the script can load all dependencies and get here, then we can exit.
if sys.argv[1] == "--check":
    exit(0)

username = None
if sys.argv[1] == "--all":
    username = "*"
elif sys.argv[1] == "--single":
    if len(sys.argv) <= 2:
        exit(2)
    # special chars reserved by LDAP that must be escaped with \ followed by their hex value
    rx = re.compile("[" + re.escape("()&|=!><~*/\\") + "]")
    username = rx.sub(lambda match: "\\" + format(ord(match.group(0)), "x"), sys.argv[2])
    if len(username) == 0:
        exit(3)

filt = filtfmt.format(username)
#eprint(filt)


conn = ldap3.Connection(host, user=dn, password=pwd, lazy=False, read_only=True, auto_range=True, auto_bind=True)

# an unsuccessful bind throws an error, so once we're here everything is fine
gen = conn.extend.standard.paged_search(base, filt, attributes = fields)

print("[")
for e in gen:
    print(json.dumps(dict(e["attributes"])), ",", sep="")
print("]")
