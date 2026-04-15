#ifndef USB_DESCRIPTORS_H
#define USB_DESCRIPTORS_H

#include "pico/stdio_usb/reset_interface.h"

enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_RESET,
    ITF_NUM_TOTAL
};

#define TUD_RPI_RESET_DESC_LEN 9

#define TUD_RPI_RESET_DESCRIPTOR(_itfnum, _stridx) \
    9, TUSB_DESC_INTERFACE, _itfnum, 0, 0, TUSB_CLASS_VENDOR_SPECIFIC, RESET_INTERFACE_SUBCLASS, RESET_INTERFACE_PROTOCOL, _stridx

#endif
