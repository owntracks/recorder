# gpx-to-rec.py
This script attempts to convert a `gpx` file into `rec` files to be used with `recorder`. I've only tested this script with gpx files with only one track and one segment from `Phonetrack`.

## Installation
`gpx-to-rec.py` depends on [`gpxpy`](https://github.com/tkrajina/gpxpy) which you can install with `pip`:
```
pip install gpxpy
```
## Usage
The script takes the `.gpx` file as an argument and creates `.rec` files in the same directory as the script.
Example:
```
python3 gpx-to-rec.py my_track.gpx
```

You can then copy those `.rec` files to `recorder`'s storage location for the corresponding user and device. By default it's in `/var/spool/owntracks/recorder/store/rec/<username>/<device>`.
Make sure to change `<username>` and `<device>` to your usecase.
After copying the files over, make sure that `recorder` has permissions to write to them:
```
sudo chown owntracks:owntracks /var/spool/owntracks/recorder/store/rec/<username>/<device>/*
```
Again, edit the command for your usecase. In my case, `recorder` uses a separate user named `owntracks`, which is why I needed to change the owner.

And there you have it. Now every point from your `gpx` file should be displayed on, for example, [`frontend`](https://github.com/owntracks/frontend).
