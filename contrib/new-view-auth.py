#!/usr/bin/env python

# OwnTracks Recorder
# Copyright (C) 2015-2016 Jan-Piet Mens <jpmens@gmail.com>
# 
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

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
