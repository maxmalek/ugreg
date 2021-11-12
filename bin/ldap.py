import ldap3
import argparse
import sys
import json
from os import environ as env

host = env["HOST"]
dn = env["DN"]
filt = env["FILTER"]
pwd = env["PASS"]
fields = env["FIELDS"].split()
base = env["BASE"]

if len(sys.argv) > 1 and sys.argv[1] == "--test":
    exit(0)
    
conn = ldap3.Connection(host, user=dn, password=pwd, lazy=False, read_only=True, auto_range=True, auto_bind=True)

# an unsuccessful bind throws an error, so once we're here everything is fine
gen = conn.extend.standard.paged_search(base, filt, attributes = fields)

print("[")
for e in gen:
    print(json.dumps(dict(e["attributes"])), ",", sep="")
print("]")
