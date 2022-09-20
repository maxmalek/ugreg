#!/usr/bin/env python3

""" basic LDAP 3pid provider"""

# Output format:
"""
{
    "media": { "email": "mail", "msisdn": "phone"},
    "data": {
        "@user:matrix.org": {
            "mail": "user@example.org",
            "displayname": "Example person",
            "phone": "1234"
        }
    }
}
"""


from mod_ldap import LDAPFetcher

from os import environ as env
import sys
import json
import re
import argparse
import collections
from collections.abc import Mapping

# this is the startup check. Make sure the script can load all dependencies and get here, then we can exit.
if len(sys.argv) > 1 and sys.argv[1] == "--check":
    exit(0)

# begin parsing cmdline
ap = argparse.ArgumentParser()

ap.add_argument("--base", type=str, required=True, metavar="LDAP_BASE")
ap.add_argument("--medium", type=str, action="append", metavar="MEDIUM or MEDIUM=FIELD")
ap.add_argument("--format", type=str, action="append", metavar="FIELD=EXPR")
ap.add_argument("--regex", nargs=3, type=str, metavar=("FIELD=EXPR", "match", "replace"), action="append")
ap.add_argument("--output", type=str, action="append", metavar="list,of,extra,fields,returnas=oldname")
ap.add_argument("--mxid", type=str, metavar="FIELD", default="mxid")
ap.add_argument("--test", type=str, metavar="JSON-file")

args = ap.parse_args()
del ap

MXID = args.mxid
assert(MXID)

# Support supplying a json file with an "env" key for easy testing
if args.test:
    with open(args.test) as fh:
        js = json.load(fh)
    env = js["env"]

# requires a=b to be split into 2 parts
def splitexpr(s:str) -> (str,str):
    xs = s.split(sep="=", maxsplit=1)
    assert(len(xs) == 2)
    return xs

# supports a=b but also just 'a' which is then the same as a=a.
def splitfield(s:str) -> (str,str):
    xs = s.split(sep="=", maxsplit=1)
    if len(xs) == 1:
        return xs * 2
    return xs

def fieldlist(s:str):
    return (splitfield(part) for part in m.split(","))

# Generator functions.
# Each of these returns a callable F(kv:dict) that, given kv, returns a string that incorporates some of the values in kv.

def genFieldLookup(expr:str):
    return expr.format_map

def genRegexLookup(expr:str, match:str, replace:str):
    rx = re.compile(match)
    def f(kv):
        m = rx.match(expr.format_map(kv))
        if m:
            return replace.format(*(m.groups()), **kv) # this can't create new keys when evaluating the replacement
    return f

FIELDGEN = {} # generators for field names, in case a key isn't present

MEDIA = {} # medium => field
FIELDS = {} # fields to include in the output. key is the key used in the output, value is the name in our work dict


for f in args.format:
    (k, fmt) = splitexpr(f)
    FIELDGEN[k] = genFieldLookup(fmt)

for (expr, match, replace) in args.regex:
    (k, fmt) = splitexpr(expr)
    rx = re.compile(match)
    FIELDGEN[k] = genRegexLookup(fmt, match, replace)

for m in args.output:
    for part in fieldlist(m):
        (k, saveas) = splitfield(m)
        FIELDS[k] = saveas

for m in args.medium:
    for part in m.split(","):
        (medium, field) = splitfield(part)
        if field not in FIELDS:
            FIELDS[field] = field
        MEDIA[medium] = field

# behaves like a dict but generates values that don't exist yet
class Accessor(Mapping):
    def __init__(self, kv:dict, gen):
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

# instantiate an Accessor with this as generator to figure out which fields
# are actually used
class UsedFields(Mapping):
    def __init__(self, gen): # forward to existing generator dict
        self.used = set()
        self.missing = set()
        self._gen = gen
    def __getitem__(self, k):
        self.used.add(k)
        g = self._gen.get(k)
        if not g:
            self.missing.add(k)
            return self
        return g
    def __iter__(self):
        return iter(self.used)
    def __len__(self):
        return len(self.used)
    def __call__(self, x):
        pass

used = UsedFields(FIELDGEN)
dummy = Accessor({}, used)
for k, f in FIELDS.items():
    _ = dummy[f]  # Just drop it on the floor

# Iterates over LDAP entries and outputs (mxid, data) as key-value pair,
# where data is formatted according to the selected FIELDS.
def outputRows(db):
    for e in db:
        a = Accessor(e, FIELDGEN)
        yield a[MXID], { k: a[f] for k,f in FIELDS.items() }

# This is a "fake dict" used for JSON serialization.
# The idea is that the json module thinks this is a complete object
# and happily serializes it, while it's actually generated on the fly
# via a generator.
# Why? We fetch key-value-pairs and immediately send them to the serializer,
# so nothing has to be stored, saving RAM for large LDAP databases.
# Also, the json serializer takes care of proper string escaping,
# so this does not have to be done here in ways that might be error-prone.
class OutputFakeDict(dict):
    def __init__(self, db):
        self._db = db
    def __iter__(self):
        raise NotImplementedError # to make sure this is never called
    def items(self): # the json module calls this to get key-value-pairs
        return outputRows(self._db)
    def __len__(self): # https://stackoverflow.com/questions/21663800/python-make-a-list-generator-json-serializable
        return 1

# -- begin output
sys.stdout.write("{")

# -- some debug infos...
sys.stdout.write("\n\"## [debug] fields missing, fetched from LDAP\": ")
json.dump(list(used.missing), sys.stdout)
sys.stdout.write(",\n\"## [debug] fields used\": ")
json.dump(list(used), sys.stdout)
sys.stdout.write(",\n\"## [debug] fields included in output\": ")
json.dump(FIELDS, sys.stdout)
# -- end debug infos

if MEDIA:
    sys.stdout.write(",\n\"media\": ")
    json.dump(MEDIA, sys.stdout)

# -- begin actual data
sys.stdout.write(",\n\"data\":\n")
json.dump(OutputFakeDict(LDAPFetcher(env).fetch(args.base, used.missing)), sys.stdout)
# -- end actual data

sys.stdout.write("\n}\n")
# -- end output
