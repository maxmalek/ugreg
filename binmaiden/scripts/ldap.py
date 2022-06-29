#!/usr/bin/env python3

""" basic LDAP 3pid provider"""

from mod_ldap import LDAPFetcher

from os import environ as env
import sys
import json
import re

# Regex to escape special chars that would mess up parsing JSON
esc = re.compile(r'([\"\\])')

# this is the startup check. Make sure the script can load all dependencies and get here, then we can exit.
if sys.argv[1] == "--check":
    exit(0)

# Support supplying a json file with an "env" key for easy testing
testmode = sys.argv[1].startswith("--test=")
if testmode:
    fn = sys.argv[1][7:]
    with open(fn) as fh:
        js = json.load(fh)
    env = js["env"]
    sys.argv.pop(1)

(medium, ldap_base, infmt, outmatch, outfmt) = sys.argv[1:6]
rx = re.compile(outmatch)

f = LDAPFetcher(env)
gen = f.fetch(ldap_base)

print('{"' + medium + '":{')
for e in gen:
    x = infmt.format(**e)
    m = rx.match(x)
    if m:
        mxid = outfmt.format(*(m.groups()), **e)
        
        # Apply basic escaping in case users have funny names
        x = esc.sub(r'\\\1', x)
        mxid = esc.sub(r'\\\1', mxid)
        
        # There will be a trailing comma after the last element but our parser doesn't care, so this is fine
        print('"' + x + '":"' + mxid + '",')
print("}}")
