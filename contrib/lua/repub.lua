-- requires: 
--   a) Compile recorder with -DLUA and configure
--   b) OTR_LUASCRIPT = ".../repub.lua"
-- also requires Lua JSON from http://regex.info/blog/lua/json

JSON = (loadfile "/etc/ot-recorder/JSON.lua")()

function otr_hook(topic, _type, data)
    otr.log("DEBUG_PUB:" .. topic .. " " .. JSON:encode(data))
    if(data['_http'] == true) then
        if(data['_repub'] == true) then
           return
        end
        data['_repub'] = true
        local payload = JSON:encode(data)
        otr.publish(topic, payload, 1, 1)
    end
end

function otr_putrec(u, d, s)
        j = JSON:decode(s)
        if (j['_repub'] == true) then
                return 1
        end
end
