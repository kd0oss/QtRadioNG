# QtRadioNG
Next generation of SDR client sofware based on QtRadio

This release takes the original QtRadio SDR sofware by John Melton G0ORX/N6LYT a few steps farther.
This is a modular client/server style system the heart of which is the open source DSP software
know as WDSP by Warren C. Pratt, NR0V. This core is wrapped in the main server portion of the system
called dspserver.  As the main server this program as to be running first.  All other software is
considered a client to dspserver. Multiple clients can connect offering different services. One 
service would be an SDR radio controller program.  This is an abstraction layer between the actual
radio hardware and the dspserver.  All communication between the dspserver and the radio client will
be standardized.  This allows creating controller software for most radios without customizing the
core DSP software.  When a radio controller program is started it will interogate the radio hardware
then package up all the relavant information about the hardware and relay it back to dspserver. 
Dspserver will store this data for later use by it or anyother clients that may require it.  
This would then require at least one other client to control and monitor the server and radio.
Typically this would be the user interface know as QtRadio but other clients could be created to
monitor or add features such as modems or audio encode/decode software or hardware.

While the software could all run on the same machine this system is more usefull when the
server and client can't be in close proximity to the radio operator such as in remote shack
operation. The dspserver and hardware really need to be located on a local network because of
the speed and bandwidth requirements.  The user interface however only requires a very small
bandwidth similar to an old dialup connection. This makes it ideal for operation over the
internet.

Software is currently in alpha stage of development.

More to come.
73
Rick KD0OSS
