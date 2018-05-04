# OpenCage

We now (since May 2018) recommend using [OpenCage](https://geocoder.opencagedata.com) as reverse geo-coding provider: their [pricing](https://geocoder.opencagedata.com/pricing) is attractive and they currently offer a free tier which allows up to 2,500 requests per day.

Use the OpenCage API in Recorder simply by setting the `--geokey` option to the string `"opencage:"` with your API key concatenated to it. (Without the substring `opencage:` the Recorder falls back to using Google in order to maintain backwards-compatibility.)

```
--geokey "opencage:xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
```

Be aware that the Recorder uses the following settings: `no_record=1&limit=1`. OpenCage documents the first as meaning it will not log the request, and that protects your privacy.

In order to use OpenCage with the Recorder using Lua, proceed as follows:

1. Make sure you've built the Recorder with support for Lua
1. Install the required Lua modules:
```bash
luarocks install lua-opencage-geocoder
```
2. Obtain an [OpenCage API key](https://geocoder.opencagedata.com/pricing), and make careful note of that.
3. Before launching the Recorder, export the API key to the Recorder's environment
```bash
export OPENCAGE_APIKEY="xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
```
4. Add a Lua script for the Recorder, which looks like [our example OpenCage Lua script](/contrib/opencage.lua)
5. Launch the Recorder using something along these lines:
```bash
ot-recorder --lua-script opencage.lua 'owntracks/#'
```
6. Note that you'll currently still require a Google Maps browser API key for the actual maps.
