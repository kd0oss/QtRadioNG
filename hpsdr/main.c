#include <string.h>
#include "qtr_net.h"
#include "radio.h"

int main(int argc, char *argv[])
{
    char dsp_server_address[17] = "127.0.0.1";

    qtr_net_init();     // WSAStartup on Windows: no socket works without it
    if (argc > 1)
        strcpy(dsp_server_address, argv[1]);
    return main_start(dsp_server_address);
}
