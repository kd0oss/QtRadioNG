/* Copyright (C)
* 2026 - Rick Schnicker KD0OSS
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

/*
 * flexradio: FLEX-6000 / FLEX-8000 / Aurora interface for dspserver
 * Receives a DAX IQ stream from the radio and transmits through a DAX TX
 * (audio) stream.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>

#include "server.h"

static void on_signal(int sig)
{
    (void)sig;
    server_request_stop();
} // end on_signal


static void usage(const char *prog)
{
    fprintf(stderr, "usage: %s [-a radio_address] [-s serial] [-c daxiq_channel] [-u udp_port] [-r radio_id] [-x] [dspserver_address]\n", prog);
    fprintf(stderr, "  -a address   connect to this radio instead of using discovery\n");
    fprintf(stderr, "  -s serial    with discovery, use the radio with this serial number\n");
    fprintf(stderr, "  -c channel   DAX IQ channel to use (default 1)\n");
    fprintf(stderr, "  -u port      local UDP port for the radio's streams (default: any free port)\n");
    fprintf(stderr, "  -r radio_id  radio id given to dspserver (default 0)\n");
    fprintf(stderr, "  -x           swap I and Q (if the spectrum comes out mirrored)\n");
    fprintf(stderr, "  dspserver_address  IPv4 address of dspserver (default 127.0.0.1)\n");
} // end usage


int main(int argc, char *argv[])
{
    SERVER_OPTIONS opts;
    struct sigaction sa;
    int c;

    memset(&opts, 0, sizeof(opts));
    opts.dsp_server_address = "127.0.0.1";
    opts.radio.daxiq_channel = 1;

    while ((c = getopt(argc, argv, "a:s:c:u:r:xh")) != -1)
    {
        switch (c)
        {
        case 'a':
            opts.radio.address = optarg;
            break;
        case 's':
            opts.radio.serial = optarg;
            break;
        case 'c':
            opts.radio.daxiq_channel = atoi(optarg);
            break;
        case 'u':
            opts.radio.udp_port = atoi(optarg);
            break;
        case 'r':
            opts.radio_id = atoi(optarg);
            break;
        case 'x':
            opts.radio.swap_iq = true;
            break;
        default:
            usage(argv[0]);
            return 1;
        }
    }
    if (optind < argc)
        opts.dsp_server_address = argv[optind];
    if (opts.radio_id < 0 || opts.radio_id > 4 || opts.radio.daxiq_channel < 1 || opts.radio.daxiq_channel > 8 ||
        opts.radio.udp_port < 0 || opts.radio.udp_port > 65535)
    {
        usage(argv[0]);
        return 1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    return server_run(&opts);
} // end main
