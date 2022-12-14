#!/usr/bin/env python3

"""Simple LDAP fetcher helper"""

import ldap3

# set sane defaults
ldap3.set_config_parameter("DEFAULT_CLIENT_ENCODING", "utf-8")
ldap3.set_config_parameter("DEFAULT_SERVER_ENCODING", "utf-8")

class LDAPFetcher:
    def __init__(self, host = None, pw = None, dn = None, filter = None):
        self.host = host or env["LDAP_HOST"]
        self.dn = dn or env["LDAP_BIND_DN"]
        self.filt = filter or env["LDAP_SEARCH_FILTER"]
        self.pwd = pw or env["LDAP_PASS"]

    """ Returns a generator expression that produces dicts with all requested search fields"""
    def fetch(self, base, fields):
        conn = ldap3.Connection(self.host, user=self.dn, password=self.pwd, lazy=False, read_only=True, auto_range=True, auto_bind=True)
        # an unsuccessful bind throws an error, so once we're here everything is fine
        gen = conn.extend.standard.paged_search(base, self.filt, attributes = fields)
        return (e["attributes"] for e in gen)
