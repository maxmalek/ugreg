#!/usr/bin/env python3

""" basic LDAP 3pid provider"""

from mod_ldap import LDAPFetcher

from os import environ as env
import sys
import json
import re
import argparse
import collections
from collections.abc import Mapping

# Regex to escape special chars that would mess up parsing JSON
ESC = re.compile(r'([\"\\])')

# this is the startup check. Make sure the script can load all dependencies and get here, then we can exit.
if len(sys.argv) > 1 and sys.argv[1] == "--check":
    exit(0)

# begin parsing cmdline
ap = argparse.ArgumentParser()

ap.add_argument("--base", type=str, required=True, metavar="LDAP_BASE")
ap.add_argument("--medium", type=str, action="append", metavar="MEDIUM=EXPR")
ap.add_argument("--field", type=str, action="append", metavar="FIELD=EXPR")
ap.add_argument("--regex", nargs=3, type=str, metavar=("FIELD=EXPR", "match", "replace"), action="append")
ap.add_argument("--fetch", type=str, required=True, metavar="list,of,ldap,fields")
ap.add_argument("--mxid", type=str, metavar="FIELD", default="mxid")
ap.add_argument("--test", type=str, metavar="JSON-file")
ap.add_argument("--extra", type=str, action="append", metavar="FIELD=EXPR")

args = ap.parse_args()
del ap
#print(args)

MXID = args.mxid

# Support supplying a json file with an "env" key for easy testing
if args.test:
    with open(args.test) as fh:
        js = json.load(fh)
    env = js["env"]

def splitexpr(s:str) -> (str,str):
    return s.split(sep="=", maxsplit=1)


# Generator functions.
# Each of these returns a callable F(kv:dict) that, given kv, returns a string that incorporates some of the values in kv.

def genFieldLookup(expr:str):
    return expr.format_map

def genRegexLookup(expr:str, match:str, replace:str):
    rx = re.compile(match)
    def f(kv):
        m = rx.match(expr.format_map(kv))
        #print(expr.format_map(kv), "->", m)
        if m:
            return replace.format(*(m.groups()), **kv) # FIXME: this can't create new keys when evaluating the replacement
    return f

FIELDGEN = {} # generators for field names

MEDIA = set()
EXTRA = set()
for m in args.medium:
    (medium, fmt) = splitexpr(m)
    MEDIA.add(medium)
    FIELDGEN[medium] = genFieldLookup(fmt)
    
for f in args.field:
    (name, fmt) = splitexpr(f)
    FIELDGEN[name] = genFieldLookup(fmt)
    
for (expr, match, replace) in args.regex:
    (k, fmt) = splitexpr(expr)
    rx = re.compile(match)
    FIELDGEN[k] = genRegexLookup(fmt, match, replace)

for m in args.extra:
    (k, fmt) = splitexpr(m)
    EXTRA.add(k)
    FIELDGEN[k] = genFieldLookup(fmt)

FETCH = args.fetch.split(sep=",") # fields to fetch from LDAP

# behaves like a dict but generates values that don't exist yet
class Accessor(Mapping):
    def __init__(self, kv:dict, gen:dict):
        self._kv = kv
        self._gen = gen
    def __getitem__(self, k):
        v = self._kv.get(k)
        if v == None:
            v = self._gen[k](self) # recursive resolve keys
            self._kv[k] = v
        return v
    def __iter__(self):
        return iter(self._kv)
    def __len__(self):
        return len(self._kv)
        

f = LDAPFetcher(env)
db = f.fetch(args.base, FETCH) # produces LDAP rows

OUT = {}
X = None
addextra = False
if EXTRA:
    X = {}
    addextra = True
for medium in MEDIA:
    OUT[medium] = {}

n = 0
for e in db:
    a = Accessor(e, FIELDGEN)
    mxid = a[MXID]
    if mxid:
        n += 1
        #for medium in MEDIA:
        #    val = a[medium]
        #    if val != None:
        #        OUT[medium][val] = mxid
        if addextra:
            ex = {}
            for k in EXTRA:
                val = a[k]
                if val != None:
                    ex[k] = val
            if ex:
                X[mxid] = ex

if X:
    OUT["_extra"] = X

json.dump(OUT, sys.stdout)

print("\n\nN = ", n)

"""
print("{")
for medium, mfmt in MEDIA:
    print('"' + medium + '":{')
    for e in db:
        a = Accessor(e, FIELDGEN)
        key = a[KEY]
        val = a[medium]
        if val:
            
            # TODO: print immediately or serialize?
            
            # Apply basic escaping in case users have funny names
            #x = ESC.sub(r'\\\1', x)
            #mxid = ESC.sub(r'\\\1', mxid)
            
            # There will be a trailing comma after the last element but our parser doesn't care, so this is fine
            #print('"' + x + '":"' + mxid + '",')
    print("},")
print("}")
"""

"""
{
    "email": { "user@bla.foo": "mxid@matrix.org", ...}
    "msisdn": { "1234": "mxid@matrix.org", ...}
    
    "_extra": {
        "@mxid:matrix.org": {
            "displayname": "Hello",
            ...
        }
    }
}


{
    "media": { "email": "mail" }
    "data": {
        "@mxid:matrix.org": {
            "email": "user@bla.foo",
            "displayname": "Hello",
            "msisdn": "1234"
        }
    }
}
"""
