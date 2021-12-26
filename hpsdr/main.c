#include "radio.h"
#include <string.h>

int main(int argc, char *argv[])
{
    char dsp_server_address[17] = "127.0.0.1";
    
    if (argc > 1)
        strcpy(dsp_server_address, argv[1]);
    main_start(dsp_server_address);
    return 0;
}
