#!/usr/bin/env python3

"""Simple LDAP fetcher helper"""

import ldap3

class LDAPFetcher:
    def __init__(self, env):
        self.host = env["LDAP_HOST"]
        self.dn = env["LDAP_BIND_DN"]
        self.filt = env["LDAP_SEARCH_FILTER"]
        self.pwd = env["LDAP_PASS"]
        self.fields = env["LDAP_SEARCH_FIELDS"].split()

    """ Returns a generator expression that produces dicts with all requested search fields"""
    def fetch(self, base):
        conn = ldap3.Connection(self.host, user=self.dn, password=self.pwd, lazy=False, read_only=True, auto_range=True, auto_bind=True)
        # an unsuccessful bind throws an error, so once we're here everything is fine
        gen = conn.extend.standard.paged_search(base, self.filt, attributes = self.fields)
        return (e["attributes"] for e in gen)
