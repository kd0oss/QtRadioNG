/**
* \file main.c
* \brief Main file for the GHPSDR Software Defined Radio Graphic Interface. 
* \author John Melton, G0ORX/N6LYT, Doxygen Comments Dave Larsen, KV0S
* \version 0.1
* \date 2009-04-11
*
*
* \mainpage GHPSDR 
*  \image html ../ghpsdr.png
*  \image latex ../ghpsdr.png "Screen shot of GHPSDR" width=10cm
*
* \section A Linux based, GTK2+, Radio Graphical User Interface to HPSDR boards through DttSP without Jack.  
* \author John Melton, G0ORX/N6LYT
* \version 0.1
* \date 2009-04-11
* 
* \author Dave Larsen, KV0S, Doxygen comments
*
* These files are design to build a simple 
* high performance  interface under the Linux  operating system.  
*
* This is still very much an Alpha version. It does still have problems and not everything is 
* completed.
*
* To build the application there is a simple Makefile.
*
* To run the application just start ghpsdr once it is built.
*
* Currently it does not include any code to load the FPGA so you must run inithw before
* running the application. You must also have the latest FPGA code.
*
* Functionally, each band has 3 bandstacks. The frequency/mode/filter settings will be 
* saved when exiting the application for all the bandstack entries.
*
* Tuning can be accomplished by left mouse clicking in the Panadapter/Waterfall window to 
* move the selected frequency to the center of the current filter. A right mouse click will 
* move the selected frequency to the cursor. You can also use the left mouse button to drag 
* the frequency by holding it down while dragging. If you have a scroll wheel, moving the 
* scroll wheel will increment/decrement the frequency by the current step amount.
*
* You can also left mouse click on the bandscope display and it will move to the selected frequency.
* 
* The Setup button pops up a window to adjust the display settings. There are no tests 
* currently if these are set to invalid values.
*
*
* There are some problems when running at other than 48000. Sometimes the audio output will 
* stop although the Panadapter/Waterfall and bandscope continue to function. It usually 
* requires intihw to be run again to get the audio back.
*
*
* Development of the system is documented at 
* http://javaguifordttsp.blogspot.com/
*
* This code is available at 
* svn://206.216.146.154/svn/repos_sdr_hpsdr/trunk/N6LYT/ghpsdr
*
* More information on the HPSDR project is availble at 
* http://openhpsdr.info
*
*/

/* Copyright (C) 
* 2009 - John Melton, G0ORX/N6LYT, Doxygen Comments Dave Larsen, KV0S
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2
* of the License, or (at your option) any later version.
* 
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
* 
*/
#define false 0
#define true  1

const char *version = "20210312;-primary"; //YYYYMMDD; text desc

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <getopt.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/param.h>

#include "server.h"
#include "wdsp.h"
#include "audiostream.h"
#include "hardware.h"
#include "version.h"
#include "G711A.h"
#include "util.h"
#include "main.h"

char propertyPath[128];

enum {
    OPT_OFFSET = 1,
    OPT_TIMING,
    OPT_LO,
    OPT_HPSDR,
    OPT_NOCORRECTIQ,
    OPT_HPSDRLOC
};

struct option longOptions[] = {
{"offset",required_argument, NULL, OPT_OFFSET},
{"timing",no_argument, NULL, OPT_TIMING},
{"lo",required_argument, NULL, OPT_LO},
{"hpsdr",no_argument, NULL, OPT_HPSDR},
{"nocorrectiq",no_argument, NULL, OPT_NOCORRECTIQ},
{0,0,0,0}
};

char* shortOptions="";

void signal_shutdown(int signum);

/* --------------------------------------------------------------------------*/
/** 
* @brief Process program arguments 
* 
* @param argc
* @param argv
*/
/* ----------------------------------------------------------------------------*/
void processCommands(int argc, char** argv, struct dspserver_config *config) 
{
    int c;
    while ((c=getopt_long(argc, argv, shortOptions, longOptions,NULL)) != -1) 
    {
        switch (c) 
        {
        case OPT_OFFSET:
            config->offset = atoi(optarg);
            break;
        case OPT_TIMING:
            client_set_timing();
            break;
        case OPT_LO:
            /* global */
            LO_offset = atoi(optarg);
            break;
        case OPT_HPSDR:
            hw_set_canTx(true);
            break;
        case OPT_HPSDRLOC:
            hw_set_harware_control(true);
            break;
        case OPT_NOCORRECTIQ:
            config->no_correct_iq = 1;
            break;

        default:
            fprintf(stderr,"Usage: \n");
            fprintf(stderr,"            --offset 0 \n");
            fprintf(stderr,"            --lo 0 (if no LO offset desired in DDC receivers, or 9000 in softrocks\n");
            fprintf(stderr,"            --hpsdr (if can transmit\n");
            fprintf(stderr,"            --hpsdrloc (if using hardware with LOCAL mike and headphone)\n");
            fprintf(stderr,"            --nocorrectiq (select if using non QSD receivers, like Hermes, Perseus, HiQSDR, Mercury)\n");
            exit(1);

        }
    }
} // end processCommands

/* --------------------------------------------------------------------------*/
/** 
* @brief  Main - it all starts here
* 
* @param argc
* @param argv[]
* 
* @return 
*/
/* ----------------------------------------------------------------------------*/

struct dspserver_config config;

int main(int argc, char* argv[]) 
{
    memset(&config, 0, sizeof(config));

    // Register signal and signal handler
    signal(SIGINT, signal_shutdown);

    strcpy(config.server_address, "127.0.0.1"); // localhost
    strcpy(config.share_config_file, getenv("HOME"));
    strcat(config.share_config_file, "/dspserver.conf");

    processCommands(argc, argv, &config);

    fprintf(stderr, "Reading conf file %s\n", config.share_config_file);
    fprintf(stderr,"DSPserver (Version %s)\n", VERSION);
    printversion();

    // create the main thread responsible for listen TCP socket
    // on the read callback:
    //    accept and interpret the commands from remote GUI
    //    parse mic data from remote and enque them into Mic_audio_stream queue
    //    see client.c
    //
    // on the write callback:
    //    read the audio_stream_queue and send into the TCP socket
    //
    server_init(0);
    audio_stream_init(0);
    audio_stream_reset();
    G711A_init();

    // create and start iq_thread in hw.c in order to
    // receive iq stream from hardware server
    // process it in WDSP
    // makes the sample rate adaption for resulting audio
    // puts audio stream in a queue (via calls to audio_stream_queue_add
    // in audio_stream_put_samples() in audiostream.c)
    //
    // in case of HPSDR hardware (that is provided with a local D/A converter
    // sends via hw_send() the audio back to the hardware server

    hw_init();

    // create and start the tx_thread (see client.c)
    // the tx_thread reads the Mic_audio_stream queue, makes the sample rate adaption
    // process the data into WDSP in order to get the modulation process done,
    // and sends back to the hardware server process (via hw_send() )
 //   tx_init();

    while (1)
    {
        sleep(10000);
    }

    return 0;
} // end main


void signal_shutdown(int signum)
{
    // catch a ctrl c etc
    printf("Caught signal %d\n",signum);
    
    exit(signum);
} // end signal_shutdown

