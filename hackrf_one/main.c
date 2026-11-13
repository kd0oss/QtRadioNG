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
 * hackrf_one: HackRF One interface for dspserver (receive only).
 *
 * usage: hackrf_one [-s serial] [-r radio_id] [-f bandscope_fps] [dspserver_address]
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
    fprintf(stderr, "usage: %s [-s serial] [-r radio_id] [-f bandscope_fps] [-l seconds] [dspserver_address]\n", prog);
    fprintf(stderr, "  -s serial         open the HackRF with this serial number (suffix)\n");
    fprintf(stderr, "  -r radio_id       radio id given to dspserver (default 0)\n");
    fprintf(stderr, "  -f fps            bandscope frames per second (default 15)\n");
    fprintf(stderr, "  -l seconds        report the ADC input level this often (default: only when it changes)\n");
    fprintf(stderr, "  dspserver_address IPv4 address of dspserver (default 127.0.0.1)\n");
} // end usage


int main(int argc, char *argv[])
{
    SERVER_OPTIONS opts;
    struct sigaction sa;
    int c;

    memset(&opts, 0, sizeof(opts));
    opts.dsp_server_address = "127.0.0.1";
    opts.radio_id = 0;
    opts.wb_fps = 15;
    opts.level_interval = 0;

    while ((c = getopt(argc, argv, "s:r:f:l:h")) != -1)
    {
        switch (c)
        {
        case 's':
            opts.serial = optarg;
            break;
        case 'r':
            opts.radio_id = atoi(optarg);
            break;
        case 'f':
            opts.wb_fps = atoi(optarg);
            break;
        case 'l':
            opts.level_interval = atoi(optarg);
            break;
        default:
            usage(argv[0]);
            return 1;
        }
    }
    if (optind < argc)
        opts.dsp_server_address = argv[optind];
    if (opts.radio_id < 0 || opts.radio_id > 4 || opts.wb_fps < 1 || opts.wb_fps > 50 || opts.level_interval < 0)
    {
        usage(argv[0]);
        return 1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;      // no SA_RESTART: blocking calls return EINTR
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    return server_run(&opts);
} // end main
