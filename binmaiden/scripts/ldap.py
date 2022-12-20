#!/usr/bin/env python3

""" basic LDAP user provider"""

# Output format:
"""
{
    (... some unnecessary entries for debugging...)
    "data": {
        "@user:matrix.org": {
            "mail": "user@example.org",
            "displayname": "Example person",
            "phone": "1234"
        }
        (...more users...)
    }
}
"""

# stdlib deps
from os import environ as env
import sys
import json
import re
import argparse
import collections
from collections.abc import Mapping

# External deps
import ldap3

# set sane defaults
ldap3.set_config_parameter("DEFAULT_CLIENT_ENCODING", "utf-8")
ldap3.set_config_parameter("DEFAULT_SERVER_ENCODING", "utf-8")

"""Simple LDAP fetcher helper"""
class LDAPFetcher:
    def __init__(self, host, pw, dn, filter):
        self.host = host
        self.dn = dn
        self.filt = filter
        self.pwd = pw

    """ Returns a generator expression that produces dicts with all requested search fields"""
    def fetch(self, base, fields):
        conn = ldap3.Connection(self.host, user=self.dn, password=self.pwd, lazy=False, read_only=True, auto_range=True, auto_bind=True)
        # an unsuccessful bind throws an error, so once we're here everything is fine
        gen = conn.extend.standard.paged_search(base, self.filt, attributes = fields)
        return (e["attributes"] for e in gen)


# Make sure everything is utf-8, especially when piping
sys.stdout.reconfigure(encoding='utf-8')

# this is the startup check. Make sure the script can load all dependencies and get here, then we can exit.
if len(sys.argv) > 1 and sys.argv[1] == "--check":
    exit(0)

# begin parsing cmdline
ap = argparse.ArgumentParser()

ap.add_argument("--format", type=str, action="append", metavar="FIELD=EXPR")
ap.add_argument("--default", type=str, action="append", metavar="FIELD=VALUE")
ap.add_argument("--regex", nargs=3, type=str, metavar=("FIELD=EXPR", "match", "replace"), action="append")
ap.add_argument("--output", type=str, action="append", metavar="list,of,extra,fields,returnas=oldname")
ap.add_argument("--mxid", type=str, metavar="FIELD", default="mxid")
ap.add_argument("--ldap-base", type=str, dest="base")
ap.add_argument("--ldap-binddn", type=str, dest="binddn")
ap.add_argument("--ldap-pass", type=str, dest="pw")
ap.add_argument("--ldap-filter", type=str, dest="filter")
ap.add_argument("--ldap-host", type=str, dest="host")

args = ap.parse_args()
del ap

MXID = args.mxid
assert(MXID)

# optional, if not present env vars are used
ldap_base = args.base or env["LDAP_BASE"]
ldap_binddn = args.binddn or env["LDAP_BIND_DN"]
ldap_pass = args.pw or env["LDAP_PASS"]
ldap_filter = args.filter or env["LDAP_SEARCH_FILTER"]
ldap_host = args.host or env["LDAP_HOST"]

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
# Return string on success, None or throw on failure

def genFieldLookup(expr:str):
    return expr.format_map

def genRegexLookup(expr:str, match:str, replace:str):
    rx = re.compile(match)
    return lambda kv: replace.format(*(rx.match(expr.format_map(kv)).groups()), **kv) # Throw on failed match

FIELDGEN = {} # generators for field names, in case a key isn't present
DEFAULTS = {} # default values if field is not present or regex doesn't match

FIELDS = {} # fields to include in the output. key is the key used in the output, value is the name in our work dict

for f in args.default:
    (k, val) = splitexpr(f)
    DEFAULTS[k] = val
    
for f in args.format:
    (k, fmt) = splitexpr(f)
    FIELDGEN[k] = genFieldLookup(fmt)

for (expr, match, replace) in args.regex:
    (k, fmt) = splitexpr(expr)
    rx = re.compile(match)
    FIELDGEN[k] = genRegexLookup(fmt, match, replace)

for m in args.output:
    for (k, saveas) in fieldlist(m):
        FIELDS[k] = saveas


# behaves like a dict but generates values that don't exist yet
class Accessor(Mapping):
    def __init__(self, kv:dict, gen, defaults:dict):
        self._kv = kv
        self._gen = gen
        self._defaults = defaults
    def __getitem__(self, k):
        v = self._kv.get(k)
        if v is None:
            try:
                v = self._gen[k](self) # recursive resolve keys, thows on fail
            except:
                pass
            if v is None:
                v = self._defaults.get(k, v) # Keep original value if not present
            if v is None: # None should never end up in the cache (str.format_map() would insert None as "None")
                raise KeyError() # So instead we just throw, making everything fail until something has a default
            self._kv[k] = v
        return v
    def __iter__(self):
        return iter(self._kv)
    def __len__(self):
        return len(self._kv)

# Helper to figure out which fields are actually used
class UsedFields(Mapping):
    def __init__(self, gen): # forward to existing generator dict
        self.used = set()
        self.missing = set()
        self.generated = set()
        self._gen = gen
    def __getitem__(self, k):
        self.used.add(k)
        try:
            self._gen[k](self)
        except:
            pass
        if self._gen.get(k):
            self.generated.add(k)
        else:
            self.missing.add(k)
    def __iter__(self):
        return iter(self.used)
    def __len__(self):
        return len(self.used)
    def __call__(self, x):
        pass

used = UsedFields(FIELDGEN)
for k, f in FIELDS.items():
    _ = used[f] # Just drop it on the floor
_ = used[MXID] # this is always required

def optional(a, f):
    try:
        return a[f]
    except:
        return None

# Iterates over LDAP entries and outputs (mxid, data) as key-value pair,
# where data is formatted according to the selected FIELDS.
def outputRows(db):
    for e in db:
        a = Accessor(kv = e, gen = FIELDGEN, defaults = DEFAULTS)
        mxid = optional(a, MXID)
        if mxid:
            yield mxid, { k: val for k,f in FIELDS.items() if (val := optional(a, f)) is not None}

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
sys.stdout.write(",\n\"## [debug] fields generated from input\": ")
json.dump(list(used.generated), sys.stdout)
sys.stdout.write(",\n\"## [debug] total fields used\": ")
json.dump(list(used), sys.stdout)
sys.stdout.write(",\n\"## [debug] fields included in output\": ")
json.dump(FIELDS, sys.stdout)
sys.stdout.write(",\n\"## [debug] field defaults\": ")
json.dump(DEFAULTS, sys.stdout)
# -- end debug infos

# -- begin actual data
sys.stdout.write(",\n\"data\":\n")
json.dump(OutputFakeDict(LDAPFetcher(host=ldap_host, pw=ldap_pass, dn=ldap_binddn, filter=ldap_filter).fetch(ldap_base, used.missing)),
    sys.stdout,
    separators=(",",":"), # Reduce whitespace
    check_circular=False, # Saves some time
    ensure_ascii=False    # Forward UTF-8 as-is
)
# -- end actual data

sys.stdout.write("\n}\n")
# -- end output
