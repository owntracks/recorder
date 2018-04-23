- must use gmake
- place script in /usr/local/etc/rc.d/ot-recorder
- creates pid file out of $! to make restart and stop possible
- note regarding line: # REQUIRE: LOGIN mosquitto hass
     remove mosquitto and hass if not setup on this box, but let LOGIN there
     if mosquitto runs on same box REQUIRE is a must! hass is Home Assistant
