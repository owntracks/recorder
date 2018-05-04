-- https://github.com/nmdguerreiro/lua-opencage-geocoder
-- luarocks install lua-opencage-geocoder
local geocoder = require "opencage.geocoder"
local gc = geocoder.new({
	key = os.getenv("OPENCAGE_APIKEY")
})

function otr_init()
end

function otr_exit()
end

function otr_hook(topic, _type, data)
end

function otr_revgeo(topic, user, device, lat, lon)
	print("OpenCAge lookup for " .. lat .. ", " .. lon)

	local d = {}

	params = {
		abbrv = 1,
		no_record = 1
	}
	local res, status, err = gc:reverse_geocode(lat, lon, params)
	if status == 200 then
		d['cc']		= string.upper(res.results[1].components.country_code)
		d['locality']	= res.results[1].components.city
		d['addr']	= res.results[1].formatted
	else
		print("OpenCage lookup returned status=" .. status)
	end

	d['_rec']       = true		-- store in .REC file
	return d
end
