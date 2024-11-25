#!/usr/bin/env python3

# github2card.py
# Copyright (C) 2016-2024 Jan-Piet Mens <jpmens@gmail.com>
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

# Usage: github2card username tid
#
# Read the Github profile of `username' and obtain user's
# name and avatar_url to create a CARD JSON payload to
# be published retained to owntracks/username/device/info
#
# You probably want to do this:
#
# github2card.py jjolie jj  > card.json
# mosquitto_pub -t owntracks/jane/phone/info -r -f card.json
#
# Note: the two commands cannot be piplelined (mosquitto_pub -l)
# because of a bug in mosquitto_pub: https://bugs.eclipse.org/bugs/show_bug.cgi?id=478917
# If you have a newer version it should work fine.

import requests
import base64
import json
import sys

def user_profile(username, tid):
    url = "https://api.github.com/users/" + username
    r = requests.get(url)
    if r.status_code != 200:
        print("User not found; response:", r)
        sys.exit(1)

    profile = json.loads(r.content)
    name = profile.get('name')
    avatar_url = profile.get('avatar_url', None)

    if name is None:
        print("User has no name; stopping")
        sys.exit(1)

    if avatar_url is None:
        print("No avatar for this user")
        sys.exit(1)

    # sizing an avatar works only for "real" avatars; not
    # for the github-generated thingies (identicons)
    avatar_url = avatar_url + '&size=80'

    r = requests.get(avatar_url)
    if r.status_code != 200:
        print("Cannot retrieve avatar for this user:", r)
        sys.exit(1)

    f = open(username + ".png", "wb")
    f.write(r.content)
    f.close()

    card = {
        '_type': 'card',
        'tid'  : tid,
        'name' : name,
        'face' : base64.b64encode(r.content).decode('utf-8')
    }

    print(json.dumps(card))


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: %s githubuser tid" % sys.argv[0], file=sys.stderr)
        sys.exit(2)
    username = sys.argv[1]
    tid = sys.argv[2]

    user_profile(username, tid)
