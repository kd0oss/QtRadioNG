DSPServer requires the WDSP SDR core by Warren Pratt NR0V to compile and run.
You will need the Linux version from John Melton g0orx/n6lyt.
This can be downloaded from GITHUB at https://github.com/g0orx/wdsp .

Download to the dspserver directory.  Enter the WDSP directory and
run make then make install.  Go back to the dspserver directory then run
make to compile the dspserver program.

run ./dspserver to start. The server will wait for client connections.

NOTE: The first time the server is started it will create a wisdom file.
      This can take some time but once created dspserver will start
      imediately each time after that.
      
