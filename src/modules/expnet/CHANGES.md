# ExpNet: versions

What each version fixes and adds. The newest on top. Written in English: the notes are
shown as they are, in the console and on the server.

## 1.0.4 (2026-10-11)

- No change in how it works: comments of the code, and its notes on the boards in English

## 1.0.3 (2026-10-09)

- The traffic of the house also without UPnP: when the router does not speak UPnP the device tries by itself a Starlink dish, a Huawei 4G or 5G router and a router with SNMP, and reads the first one that answers
- New settings: who tells the traffic of the house, to choose one kind only; the address of the device that tells it, which can carry a port; for SNMP its community and, when the router does not say its routes, the number of the port towards the Internet
- Nothing is sent to a device with values of the factory: SNMP is asked only with a community written by the owner
- Fixed: an answer to an old search could stop the new one

## 1.0.2 (2026-10-07)

- The list of the devices reads well on a phone: each device on its own lines, with a button that opens its web page when it has one, and one that copies its address
- The type of every device, with its icon and colour: router, access point, PC, phone, camera, recorder (NVR), TV and media, smart speaker, printer, NAS, server, smart device, LoopMaxPro free and private, LoopMax v1; buttons to show one group at a time, and a type of your own for any device
- Devices recognised by their maker (from the IEEE registry), by what they tell through mDNS and UPnP, by their names; SmartiLab as the maker of the LoopMaxPro devices. On the boards with more memory (not the C3) a larger table of makers, and cameras and recorders recognised by their ports (Hikvision, Dahua, XMEye: Vultech, Videostar and many more)
- A device asleep during a search no longer shows twice, and the times stay in the list while a search runs
- UPnP: a light tells whether the router is read, looked for or missing, and the traffic tile says so. A new setting, UPnP router, reads another device by its address (a mesh node, a router in another subnet); left empty, only the router of the network
- The router is asked half the questions: two counters each round, all of them and the speed of the line once a minute
- On the C3 the search asks the network less, and ExpNet asks LoopMaxPro for fewer Wi-Fi buffers and the position over http, so that the board keeps its memory and stays in the network; the other modules on the C3 keep their values
- Quiet when nobody looks: a page out of sight (a tab behind, the screen off) asks nothing, so the device stops its pings and its questions to the router; after half an hour without anybody, and with nobody to tell (Telegram, a home system), the automatic search waits for the first look. The watched services and the PCs are always checked

## 1.0.1 (2026-10-07)

Not published: what it brought is in 1.0.2.

- The list of the devices reads well on a phone: each device on its own lines, with a button that opens its web page when it has one, and one that copies its address
- The type of every device, with its icon and colour: router, access point, PC, phone, camera, recorder (NVR), TV and media, smart speaker, printer, NAS, server, smart device, LoopMaxPro free and private, LoopMax v1; buttons to show one group at a time, and a type of your own for any device
- Devices recognised by their maker (from the IEEE registry), by what they tell through mDNS and UPnP, by their names; SmartiLab as the maker of the LoopMaxPro devices. On the boards with more memory (not the C3) a larger table of makers, and cameras and recorders recognised by their ports (Hikvision, Dahua, XMEye: Vultech, Videostar and many more)
- A device asleep during a search no longer shows twice, and the times stay in the list while a search runs
- UPnP: a light tells whether the router is read, looked for or missing, and the traffic tile says so. A new setting, UPnP router, reads another device by its address (a mesh node, a router in another subnet); left empty, only the router of the network
- The router is asked half the questions: two counters each round, all of them and the speed of the line once a minute
- On the C3 the search asks the network less, so that the board keeps its memory and stays in the network

## 1.0.0

- First version of Networks Explorer, the free edition: who is in the network, searched by itself every 15 minutes and kept, with the LoopMaxPro devices in sight and a warning for every device never seen before
- The metrics of the home: times of the router and of Internet, signal, traffic of the router through UPnP
- The services of the network watched every minute, with 7 days of history; the PCs switched on with Wake-on-LAN
- Wi-Fi networks around and the best channel
- Telegram: warnings, morning summary, quiet hours and a test button
- Tools, live gauges, speed test, MQTT and test endpoints are there to explore, with their parameters: they run in ExpNetPro, the full version
- In Home Assistant every watched service is a connectivity sensor, and every PC a switch-on button
