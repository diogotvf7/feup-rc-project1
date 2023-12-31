// Application layer protocol header.
// NOTE: This file must not be changed.

#ifndef _APPLICATION_LAYER_H_
#define _APPLICATION_LAYER_H_

#include "link_layer.h"
#include "vars.h"

// Application layer main function.
// Arguments:
//   serialPort: Serial port name (e.g., /dev/ttyS0).
//   role: Application role {"tx", "rx"}.
//   baudrate: Baudrate of the serial port.
//   nTries: Maximum number of frame retries.
//   timeout: Frame timeout.
//   filename: Name of the file to send / receive.

typedef struct
{
    unsigned int fileSizeBytes;
    unsigned int fileSize;
    unsigned int fileNameSize;
    char *filename;
} file_info;

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename);

int write_control_packet(int file, const char *filename, int type);

int receive_control_packet();
#endif // _APPLICATION_LAYER_H_
