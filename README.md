# QtRadioNG
Next generation of SDR client sofware based on QtRadio

This release takes the original QtRadio SDR software by John Melton G0ORX/N6LYT a few steps farther.
This is a modular client/server style system the heart of which is the open source DSP software
known as WDSP by Warren C. Pratt, NR0V. This core is wrapped in the main server portion of the system
called dspserver.  As the main server, this program has to be running first.  All other software is
considered a client to dspserver. Multiple clients can connect offering different services. One 
service would be an SDR radio controller program.  This is an abstraction layer between the actual
radio hardware and dspserver.  All communication between dspserver and the radio client will
be standardized.  This allows creating controller software for most radios without customizing the
core DSP software.  When a radio controller program is started it will interogate the radio hardware
then package up all the relavant information about the hardware and relay it back to dspserver. 
Dspserver will store this data for later use by it or anyother clients that may require it.  
This would then require at least one other client to control and monitor the server and radio.
Typically this would be the user interface known as QtRadio but other clients could be created to
monitor or add features such as modems or audio encode/decode software or hardware.

While the software could all run on the same machine this system is more usefull when the
server and client can't be in close proximity to the radio operator such as in remote shack
operation. The dspserver and hardware really need to be located on the same local network because of
the speed and bandwidth requirements.  The user interface however only requires a very small
bandwidth similar to an old dialup connection. This makes it ideal for operation over the
internet.

## Building

The terminal programs (dspserver, hpsdr, hackrf_one, flexradio and the
protocol plugins) build with CMake 3.16 or later:

    cmake -B build -DWDSP_DIR=/path/to/wdsp
    cmake --build build -j
    ctest --test-dir build            # tests (Python 3 with numpy)
    sudo cmake --install build        # optional, into /usr/local/bin

The programs end up in `build/<program>/`. hackrf_one is skipped with a
warning if libhackrf is not installed. These options turn programs off:

* `-DBUILD_DSPSERVER=OFF`
* `-DBUILD_HPSDR=OFF`
* `-DBUILD_HACKRF_ONE=OFF`
* `-DBUILD_FLEXRADIO=OFF`
* `-DBUILD_PLUGINS=OFF`

`-DBUILD_TESTING=OFF` skips the tests. Each program can also be built on
its own from its directory (`cmake -S hpsdr -B build-hpsdr`). See
dspserver/README.txt for WDSP.

QtRadio keeps its qmake project (QtRadio/QtRadio.pro).

## Several radios

Several radio programs (hpsdr, hackrf_one, flexradio) can be connected to
one dspserver at the same time: start each the usual way, in any order. Up
to 5 radio programs and 5 radios (one hpsdr can serve more than one board)
are supported.

* dspserver numbers the radios in the order they first connect. That is the
  number QtRadio shows in its radio list, whatever id the radio has in its
  own program, so hpsdr's first board and a HackRF can both be radio 0
  there.
* If a radio program stops and starts again, its radio keeps its number. In
  between, clients see it with "(offline)" after its name.
* QtRadio uses one radio per session: pick it in the list that comes up
  when connecting. To use two radios at once, run a second QtRadio and pick
  the other radio there.
* Each radio program gets its own block of UDP ports from dspserver: the
  first connected uses ports 10000 to 10159 as before, the second 10200 to
  10359, the third 10400 to 10559, and so on (+200 each). Open these on a
  firewall between dspserver and a radio program on another machine. The
  radio programs connect to dspserver on TCP port 10100.

## Protocol plugins

Modes that WDSP does not handle, such as M17 digital voice, are added by
plugin programs that connect to dspserver and add a mode and a control panel
to QtRadio. See plugins/README.md.

Software is currently in alpha stage of development.

More to come.
73
Rick KD0OSS
