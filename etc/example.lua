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
