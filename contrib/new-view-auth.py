#!/usr/bin/env python

import getpass
import sys
try:
    from hashlib import md5
except ImportError:
    from md5 import md5

def digest_password(realm, username, password):
    """ construct a hash for HTTP digest authentication """
    return md5("%s:%s:%s" % (username, realm, password)).hexdigest()

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print "Usage: %s username" % sys.argv[0]
        sys.exit(1)

    username = sys.argv[1]
    realm = 'owntracks-recorder'

    pw1 = getpass.getpass("Enter password for user %s: " % username)
    pw2 = getpass.getpass("Re-enter password: ")

    if pw1 != pw2:
        print "Passwords don't match"
        sys.exit(2)

    print '"auth" : [ "%s" ]' % (digest_password(realm, username, pw1))
