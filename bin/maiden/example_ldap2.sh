#!/bin/sh

# Slightly more advanced example.
# Idea: We want the displayname in matrix to include a short phone number
# if the user has one.
# Let's assume our institution has "+00 000 " as common prefix in LDAP,
# so that we can simply regex-match any number of digits after that.
# But since not every user has a phone number the regex match can fail;
# this is where --default for a field comes in.
# So we use phonesuffix as an intermediate field
# that either contains a phone number or an empty string if no phone number can be parsed.
# The space in 'phonesuffix= ({phone})' is intentional,
# so that a space is only added if there is a phone number.

# Here, the recursive resolving works as follows:
# If the user has a number:
# - Try to resolve displayname ...
#   - Try to resolve realname...
#     - Try to resolve givenName -> use LDAP field ("John")
#     - Try to resolve sn -> use LDAP field ("Doe")
#   => realname = "John Doe"
#   - Try to resolve phonesuffix...
#     - Try to resolve phone...
#       - Regex match succees (assume "1234" was matched)
#     => format phone = '1234'
#   => resolved phonesuffix = ' (1234)'
# => resolve displayname = "John Doe (1234)"

# If the phone number regex does not match, the last steps look like this:
# ...
#   - Try to resolve phonesuffix...
#     - Try to resolve phone...
#       - Regex match fail
#       => propagate fail up
#   - phonesuffix failed
#     - look up defaults[phonesuffix]
#     => Found; phonesuffix = ''
#   => resolved phonesuffix = ''
# => resolve displayname = "John Doe"

# Leaving away --default would be incorrect, because then resolving displayname
# would fail for users that have no phone number, and the user would be skipped.

scripts/ldap.py \
--ldap-host "ldaps://ad.example.com:3269" \
--ldap-pass 'secret' \
--ldap-binddn 'OU=Users,DC=example,DC=org' \
--ldap-base 'CN=My User,OU=Users,DC=example,DC=org' \
--ldap-filter '(&(mail=*@*)(!(mail=*.DISABLED))))' \
--format 'realname={givenName} {sn}' \
--format 'displayname={realname}{phonesuffix}' \
--format 'phonesuffix= ({phone})' \
--regex 'mxid={mail}' '(.+)@example.org' '@{0}:example.org' \
--regex 'phone={telephoneNumber}' '\+00 000 (\d+)' '{0}' \
--default 'phonesuffix=' \
--output displayname,mail,realname,phone
