# Config debugging

All dump and export functions display the handle for a specific configuration key. They can be translated into a human readable form

## Handles

To save memory, each configuration key is represented by it's crc16 checksum. To enable collecting handle names and the checksum in a file, set `DEBUG_CONFIGURATION_GETHANDLE` to `1`

## Viewing handles that have been read or written

The crc16 and the names are stored in /.pvt/cfg_handles. To get a list of all handles, run a factory reset. It is possible that handles are not written during the factory reset. Those will be added as soon as they get accessed the first time

### Example `/.pvt/cfg_handles`

``` text
0000 <NVS>
ffff <INVALID>
8365 MainConfig().system.device.cfg
db24 MainConfig().system.webserver.cfg
655b MainConfig().network.settings.cfg
5f1a MainConfig().plugins.alarm.cfg
4c3d MainConfig().plugins.serial2tcp.cfg
88b3 MainConfig().plugins.mqtt.cfg
eb69 MainConfig().plugins.syslog.cfg
f83a MainConfig().plugins.ntpclient.cfg
bcd2 MainConfig().plugins.sensor.cfg
a1a7 MainConfig().system.flags.cfg
0ecb MainConfig().network.softap.cfg
e058 MainConfig().plugins.blinds.cfg
4d89 MainConfig().plugins.dimmer.cfg
be67 MainConfig().plugins.clock.cfg
e56c MainConfig().plugins.ping.cfg
4d02 MainConfig().plugins.weatherstation.cfg
4569 MainConfig().plugins.remote.cfg
afdb MainConfig().system.firmware.MD5
1643 MainConfig().system.firmware.PluginBlacklist
f743 MainConfig().network.wifi.SoftApSSID
78c7 MainConfig().network.wifi.SSID0
0d3d MainConfig().system.device.Name
c096 MainConfig().network.wifi.Password0
dc09 MainConfig().network.wifi.SoftApPassword
7058 MainConfig().system.device.Password
cf80 MainConfig().system.device.Title
d4a1 MainConfig().plugins.syslog.Hostname
f95f MainConfig().plugins.ntpclient.Server1
f81f MainConfig().plugins.ntpclient.Server2
38de MainConfig().plugins.ntpclient.Server3
65f7 MainConfig().plugins.ntpclient.PosixTimezone
43e6 MainConfig().plugins.mqtt.Hostname
e171 MainConfig().plugins.mqtt.Username
8613 MainConfig().plugins.mqtt.Password
2531 MainConfig().plugins.mqtt.BaseTopic
7ff7 MainConfig().system.device._ObjectId
aa09 MainConfig().plugins.mqtt.AutoDiscoveryPrefix
4cc7 MainConfig().plugins.mqtt.AutoDiscoveryName
38e0 MainConfig().plugins.ntpclient.TimezoneName
b806 MainConfig().network.wifi.SSID1
0057 MainConfig().network.wifi.Password1
```
