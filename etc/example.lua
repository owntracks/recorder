local file

function otr_init()
	otr.log("example.lua starting; writing to /tmp/lua.out")
	file = io.open("/tmp/lua.out", "a")
	file:write("written by OwnTracks Recorder version " .. otr.version .. "\n")
end

function otr_hook(topic, _type, data)
	local timestr = otr.strftime("It is %T in the year %Y", 0)
	print("L: " .. topic .. " -> " .. _type)
	addr = data['addr']
	if addr == nil then
		addr = 'unknown'
	end
	file:write(timestr .. " " .. topic .. " lat=" .. data['lat'] .. addr .. "\n")
end

function otr_exit()
end

-- invoked when a publish over httpmode is received
-- data is a Lua table with the OwnTracks payload in it,
-- keys are those known from our JSON

function otr_httpobject(user, device, _type, data)

	resp = {}

	if _type == 'transition' and data['desc'] == 'Fireplace' then
		resp["_type"]	= "cmd"
		resp["tid"]	= data['tid']
		resp["tst"]	= os.time()
		resp["action"]	= "action"
		resp["comment"]	= "by Lua in otr_httpobject()"

		if data['event'] == 'enter' then
			resp["url"]	= "http://www.lua.org"
			resp["extern"]	= false
		else
			resp["url"] = nil
		end
	end

	return resp
end
