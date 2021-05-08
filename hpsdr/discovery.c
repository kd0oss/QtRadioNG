/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
*
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

#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>

//#include "band.h"
//#include "agc.h"
#include "alex.h"
//#include "channel.h"
//#include "vfo.h"
#include "discovered.h"
#include "old_discovery.h"
#include "new_discovery.h"
#include "radio.h"
#include "new_protocol.h"
#include "old_protocol.h"
#include "radio.h"
#ifdef USBOZY
#include "ozyio.h"
#endif
#ifdef GPIO
#include "gpio.h"
#include "configure.h"
#endif
#include "protocols.h"

bool enable_protocol_1 = true;
bool enable_protocol_2 = true;
bool autostart = true;

static DISCOVERED *d;

#define IPADDR_LEN 20
static char ipaddr_tcp_buf[IPADDR_LEN] = "10.10.10.10";
char *ipaddr_tcp = &ipaddr_tcp_buf[0];


#ifdef GPIO
static bool gpio_cb (void *data) {
    //configure_gpio(discovery_dialog);
    return TRUE;
}

static void gpio_changed_cb(void *data) {
    //  controller=gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
    gpio_set_defaults(controller);
    gpio_save_state();
}
#endif

void discovery()
{
    selected_device=0;
    devices=0;

    // Try to locate IP addr
    FILE *fp = fopen("ip.addr", "r");
    if (fp)
    {
        char *c = fgets(ipaddr_tcp, IPADDR_LEN, fp);
        fclose(fp);
        ipaddr_tcp[IPADDR_LEN-1]=0;
        // remove possible trailing newline char in ipaddr_tcp
        int len=strnlen(ipaddr_tcp, IPADDR_LEN);
        while (--len >= 0)
        {
            if (ipaddr_tcp[len] != '\n') break;
            ipaddr_tcp[len]=0;
        }
    }
#ifdef USBOZY
    //
    // first: look on USB for an Ozy
    //
    fprintf(stderr,"looking for USB based OZY devices\n");

    if (ozy_discover() != 0)
    {
        discovered[devices].protocol = ORIGINAL_PROTOCOL;
        discovered[devices].device = DEVICE_OZY;
        discovered[devices].software_version = 10;              // we can't know yet so this isn't a real response
        discovered[devices].status = STATE_AVAILABLE;
        strcpy(discovered[devices].name,"Ozy on USB");

        strcpy(discovered[devices].info.network.interface_name,"USB");
        devices++;
    }
#endif


    if (enable_protocol_1)
    {
        printf("Protocol 1 ... Discovering Devices\n");
        old_discovery();
    }

    if (enable_protocol_2)
    {
        printf("Protocol 2 ... Discovering Devices\n");
        new_discovery();
    }

    fprintf(stderr,"discovery: found %d devices\n", devices);


    int row=0;
    if (devices == 0)
    {
        printf("No local devices found!\n");
        row++;
    }
    else
    {
        char version[16];
        char text[256];
        for (row=0;row<devices;row++)
        {
            d = &discovered[row];
            fprintf(stderr, "%p Protocol=%d name=%s\n", d, d->protocol, d->name);
            sprintf(version,"v%d.%d",
                    d->software_version / 10,
                    d->software_version % 10);
            switch (d->protocol)
            {
            case ORIGINAL_PROTOCOL:
            case NEW_PROTOCOL:
#ifdef USBOZY
                if (d->device==DEVICE_OZY) {
                    sprintf(text,"%s (%s) on USB /dev/ozy", d->name, d->protocol==ORIGINAL_PROTOCOL?"Protocol 1":"Protocol 2");
                } else {
#endif
                    sprintf(text,"%s (%s %s) %s (%02X:%02X:%02X:%02X:%02X:%02X) on %s: ",
                            d->name,
                            d->protocol==ORIGINAL_PROTOCOL?"Protocol 1":"Protocol 2",
                            version,
                            inet_ntoa(d->info.network.address.sin_addr),
                            d->info.network.mac_address[0],
                            d->info.network.mac_address[1],
                            d->info.network.mac_address[2],
                            d->info.network.mac_address[3],
                            d->info.network.mac_address[4],
                            d->info.network.mac_address[5],
                            d->info.network.interface_name);
#ifdef USBOZY
                }
#endif
                break;
            }

            // if not available then cannot start it
            if (d->status != STATE_AVAILABLE)
            {
                printf("In Use\n");
            }

            // if not on the same subnet then cannot start it
            if ((d->info.network.interface_address.sin_addr.s_addr&d->info.network.interface_netmask.sin_addr.s_addr) != (d->info.network.address.sin_addr.s_addr&d->info.network.interface_netmask.sin_addr.s_addr))
            {
                printf("Subnet!\n");
            }
        }
    }


#ifdef GPIO
    controller=CONTROLLER2_V2;
    gpio_set_defaults(controller);
    gpio_restore_state();

    //   GtkWidget *gpio=gtk_combo_box_text_new();
    //   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(gpio),NULL,"No Controller");
    //   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(gpio),NULL,"Controller1");
    //   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(gpio),NULL,"Controller2 V1");
    //   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(gpio),NULL,"Controller2 V2");
    //   gtk_grid_attach(GTK_GRID(grid),gpio,0,row,1,1);

    //   gtk_combo_box_set_active(GTK_COMBO_BOX(gpio),controller);
    //   g_signal_connect(gpio,"changed",G_CALLBACK(gpio_changed_cb),NULL);
#endif

    row++;

    // autostart if one device and autostart enabled
    printf("%s: devices=%d autostart=%d\n", __FUNCTION__, devices, autostart);
}
