# LoopMaxPro: versions

What each version fixes and adds. The newest on top. Written in English: the notes are
shown as they are, in the console and on the server.
The versions of the modules have their notes in the folder of each module.

## 1.0.15 (2026-10-18)

- A module can read the signal of the Wi-Fi the device is on, and the local time of any moment, its daylight saving included: what a screen needs to show the signal and the hour of the last event
- The commands say which ones are of every day, simple and safe, for who commands the device without the rights of the advanced. Those that need no value to choose are buttons among the things too: the channels have All on and All off besides Invert
- A button that undoes what does not come back says so, as its command does, and Home Assistant never gets it
- The settings tell their unit, whether they are for who looks after the device, and the values of a choice; the numbers of the things tell their limits where they are known
- The names of the commands, arguments and settings of every module, in English and Italian, travel with the console to a server. A phrase a page shares with another module, or one the console has already, is kept once

## 1.0.14 (2026-10-14)

What a device shows to Home Assistant and to a server says more, and some of it can be set from there.

- A number, a text and a choice among fixed values can be set from Home Assistant, each within its own limits: they appear there as a number, a text and a select
- Each light has its effect to choose and the speed of the effect to set
- The channels that are switched have a button that inverts them all
- A number tells its limits and when it was measured: a value that stands still is no longer the same as a sensor that is silent
- A device with a server keeps the measures it makes while the server cannot be reached, each with its time, and gives them when the server is back. They are kept in memory: a restart loses them
- The list of the commands of a module says of each one whether it only reads, whether it reaches outside the device, whether its answer is to keep

## 1.0.13 (2026-10-11)

Nothing changes in how a device works: what belongs to one product leaves the system and comes with the product.

- A firmware can carry a service of its own, the service of the product, which can stop the modules of the device and say why
- A bridge can add tabs of its own and a notice to the LoopMaxPro space of the console
- The position from the GPS of a phone, on the local network, is offered when the bridge of the device gives a page for it. Without one the coordinates are typed or pasted, as before
- An old way to ask the position, no longer in use, is removed
- The console is 4 KB smaller on every board

## 1.0.12 (2026-10-09)

- The free memory of the classic ESP32 is measured as it is: the figures were about 35 KB higher than what a program can take. On these boards the memory shown is now lower, and true; S3, C3 and C6 do not change
- A module short of memory on a board can ask, in its module.json, for a guard that keeps a secure connection waiting until there is memory for it, for the console to send its requests a few at a time, and for smaller buffers of the broker of the house. All of it is off in every other firmware
- Console: the end of a command that finishes later was lost when it arrived before the answer to the command itself, and the page waited a minute for nothing. Fixed
- Two tools for who develops, off in every firmware: the largest blocks of memory in use, and where the packets of the network spend their time, written on the serial port

## 1.0.11 (2026-10-07)

- A module can ask for its own values of LoopMaxPro on the boards where it needs them (memory, buffers, how the position is asked), in its module.json: they reach only the firmware with that module, never the others, and a product can still change them
- The two values of 1.0.10 for the C3, fewer Wi-Fi buffers and the position asked over http, are now the ones of ExpNet on the C3: every other module on the C3 works as in 1.0.9
- A question over UDP can take its answer from any port of the address asked: some routers answer UPnP from a port of their own
- The console carries the notes of the newest version only, closed until opened and more compact: 3 KB less on every board; the history is on the server

## 1.0.10 (2026-10-07)

Not published: what it brought is in 1.0.11.

- More free memory on the boards that have little, as the C3: there the position of the device is asked over http, not https (a secure connection took 50 KB at every start), and the Wi-Fi keeps 16 buffers instead of 32
- A question over UDP can take its answer from any port of the address asked: some routers answer UPnP from a port of their own

## 1.0.9 (2026-10-06)

The ground of ExpNet, the explorer of the network: the device asks the network more, and a module can answer the programs of the house.

- Questions to the network: the way to an address, router after router; a question and its answer over UDP, as the name servers want; many TCP ports at once; mDNS and UPnP looked for by the kind of service
- Requests to the web: their time without the wait in the queue, the headers of the answer, and quiet requests for a module that reports the failures in its own way
- A module can answer the programs of the local network at paths of its own, without the login of the console and with the headers a browser wants; only from the local network
- A module keeps as much as the memory of the board allows: little on the C3, much more on the boards with PSRAM
- The settings of the full version of a module are shown in its free version, switched off and locked
- The page of a module can read the network and the other parts of the device

## 1.0.8 (2026-10-05)

What the modules show reaches the server too, as it reaches Home Assistant: the ground for commands and notifications from Telegram for every product. Nothing changes in how the devices work.

- The things of every module, its switches, lights, values and buttons, are told to the server through the bridge: their values with the state of the module, what they are on request
- One command sets a thing, the same for every module: on, off, press, the brightness and the colour of a light
- A hub tells the things of its traps once, and their values a page at a time, only the ones that changed
- It needs no MQTT broker at home. ESP8266 boards do not have it

## 1.0.7 (2026-10-05)

Home Assistant and the voice assistants behind it: said clearly in the console, and told better by the devices.

- The MQTT tab says what it is for: the device in your Home Assistant and, from there, in Amazon Alexa, Google Home and Siri, with the steps to do it and what the device has presented
- The Network tab points to it, with a button that opens the MQTT settings
- A warning when the address of the broker is written but MQTT was left off
- A module can tell Home Assistant which of its things are sockets, and which are for who looks after the device, such as a broken sensor, a battery or a radio link: these are kept apart as diagnostics and are not offered to the voice assistants

## 1.0.6 (2026-10-05)

Devices find each other on the network, present themselves to Home Assistant, and know where they are. ESP8266 boards work as before, without these three.

- Find devices, a new tab of the console: the other LoopMaxPro devices on the network, each one opening its console with a tap, and the devices switched on but not on a network yet, with the steps to set them up
- Every device announces itself on the local network as a LoopMaxPro device, with its name, its product and its version
- Home Assistant: give the device the address of your MQTT broker in the MQTT tab, and it presents its switches, lights, sensors and buttons by itself, and takes commands from Home Assistant
- Where the device is: on a network the console asks for it once, from the GPS of the phone or as coordinates typed in. A GPS on the board comes first, then the owner, then the network; a city typed by hand is a name, not a point
- On its own access point the device takes the time, and the first time the time zone, from the phone or PC that opens the console
- Addressable LEDs no longer stay with wrong colours after a disturbance on the wire: what they show is sent again every 2 seconds, and on ESP32, S2 and S3 boards a short strip leaves in one piece
- The device counts its starts and remembers the last six, with the reason of each, also through a power loss
- The MQTT tab tells the real default base of the topics: lmp/ and the host name

## 1.0.5 (2026-10-03)

A correction for one product. Nothing changes for the other devices.

- A product no longer offers the simulated devices left from the first tests

## 1.0.4 (2026-10-03)

What a LoRa hub needs. Nothing changes for the devices already in use.

- LoRa: a long preamble to wake devices that sleep, and the sync word of a network set by a module
- The banner "not on a network yet" can be hidden, for a device kept in access point on purpose
- On a phone the buttons of a banner go under its text

## 1.0.3 (2026-10-02)

What the system gained to host a new module. Nothing changes for the devices already in use.

- Modules can use the SPI bus of the board, shared as the I2C ones: screens and other chips
- Pins can time a pulse and count edges with their interrupts: ultrasonic sensors, reeds and counters, with a filter for the spikes of long wires

## 1.0.2 (2026-10-02)

The command lock is now visible on fast boards, and manual commands follow one rule with schedules.

- The lock of the console while a command runs shows at once and lasts long enough to be seen: in 1.0.1 a board on the local network answered before it appeared
- Invert, All on and All off follow one rule: a channel is left alone only while a schedule is running on it; outside that time they all work
- A command that a running schedule does not allow says so: "Command not allowed", with the channels it left
- Invert no longer asks for confirmation
- The versions in the overview are one line each, opened with a tap

## 1.0.1 (2026-10-01)

A console that is safer with slow boards and with the remote console.

- One command at a time: while the board runs a command the console locks and shows it, so repeated taps no longer send duplicate commands
- It applies to every module: switches, invert, all on and all off; the sliders of the lights always send the last value, never two at once
- The console can be stopped and started again on another device: the remote console switches device without reloading the page
- The overview lists the versions of LoopMaxPro, each with its notes

## 1.0.0 (2026-10-01)

The first version of LoopMaxPro.

- First version: system, local web console, network with a fallback access point, firmware update with trial and automatic rollback
- The local console is inside the program: every update brings the console too and leaves settings and data untouched
- The board recovers by itself when it hangs, and after the restart the log says what had happened
- Updates from the server are offered in the local console too, when the system or a module is newer
