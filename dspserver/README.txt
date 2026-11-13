DSPServer requires the WDSP SDR core by Warren Pratt NR0V to compile and run.
You will need the Linux version from John Melton g0orx/n6lyt.
This can be downloaded from GITHUB at https://github.com/g0orx/wdsp .

Download to the dspserver directory.  Enter the WDSP directory and
run make then make install.  Go back to the dspserver directory and build
dspserver with CMake (3.16 or later):

    cmake -B build
    cmake --build build

If WDSP is somewhere else, give its directory:

    cmake -B build -DWDSP_DIR=/path/to/wdsp

dspserver is linked with the path to the WDSP library it was built against,
so LD_LIBRARY_PATH is not needed.  It also needs fftw3, libsamplerate,
OpenSSL, libevent and OpenMP.  The top-level CMakeLists.txt builds dspserver
together with the radio interface programs.

Run ./build/dspserver from the dspserver directory to start (it keeps its
wisdom file there). The server will wait for client connections.

NOTE: The first time the server is started it will create a wisdom file.
      This can take some time but once created dspserver will start
      imediately each time after that.
      
