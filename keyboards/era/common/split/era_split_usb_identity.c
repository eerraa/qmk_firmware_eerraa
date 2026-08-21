// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_usb_identity.h"

#include "hardware_id.h"
#include "split_util.h"
#include "usb_descriptor.h"

#include <string.h>

#if !defined(USB_DEVICE_DESCRIPTOR_OVERRIDE_ENABLE)
#    error "era_split_usb_identity.c requires USB_DEVICE_DESCRIPTOR_OVERRIDE_ENABLE."
#endif

#if !defined(USB_PRODUCT_STRING_OVERRIDE_ENABLE)
#    error "era_split_usb_identity.c requires USB_PRODUCT_STRING_OVERRIDE_ENABLE."
#endif

#if !defined(USB_SERIAL_NUMBER_STRING_OVERRIDE_ENABLE)
#    error "era_split_usb_identity.c requires USB_SERIAL_NUMBER_STRING_OVERRIDE_ENABLE."
#endif

#if !defined(SPLIT_KEYBOARD)
#    error "era_split_usb_identity.c requires SPLIT_KEYBOARD."
#endif

#if !defined(SPLIT_HAND_PIN)
#    error "era_split_usb_identity.c requires SPLIT_HAND_PIN."
#endif

#if defined(__AVR__)
#    error "era_split_usb_identity.c is unsupported on AVR."
#endif

#if !defined(ERA_SPLIT_USB_IDENTITY_PID_LEFT)
#    error "ERA_SPLIT_USB_IDENTITY_PID_LEFT must be defined."
#endif

#if !defined(ERA_SPLIT_USB_IDENTITY_PID_RIGHT)
#    error "ERA_SPLIT_USB_IDENTITY_PID_RIGHT must be defined."
#endif

#ifndef ERA_SPLIT_USB_IDENTITY_PRODUCT_BASE
#    define ERA_SPLIT_USB_IDENTITY_PRODUCT_BASE PRODUCT
#endif

enum {
    ERA_SPLIT_USB_IDENTITY_PRODUCT_BASE_LENGTH = sizeof(ERA_SPLIT_USB_IDENTITY_PRODUCT_BASE) - 1,
    ERA_SPLIT_USB_IDENTITY_PRODUCT_LENGTH      = ERA_SPLIT_USB_IDENTITY_PRODUCT_BASE_LENGTH + 2,
    ERA_SPLIT_USB_IDENTITY_SERIAL_LENGTH       = ERA_SPLIT_USB_IDENTITY_PRODUCT_BASE_LENGTH + 3 + (sizeof(hardware_id_t) * 2),
};

typedef struct {
    USB_Descriptor_Header_t Header;
    uint16_t                UnicodeString[ERA_SPLIT_USB_IDENTITY_PRODUCT_LENGTH];
} era_split_usb_identity_product_string_t;

typedef struct {
    USB_Descriptor_Header_t Header;
    uint16_t                UnicodeString[ERA_SPLIT_USB_IDENTITY_SERIAL_LENGTH];
} era_split_usb_identity_serial_string_t;

extern const USB_Descriptor_Device_t PROGMEM DeviceDescriptor;

static USB_Descriptor_Device_t                    era_split_usb_identity_device_descriptor;
static era_split_usb_identity_product_string_t    era_split_usb_identity_product_string;
static era_split_usb_identity_serial_string_t     era_split_usb_identity_serial_string;
static bool                                       era_split_usb_identity_ready;

static void era_split_usb_identity_copy_ascii(uint16_t* target, uint8_t* index, const char* source, uint8_t length) {
    for (uint8_t i = 0; i < length; i++) {
        target[(*index)++] = (uint8_t)source[i];
    }
}

static void era_split_usb_identity_build_product_string(char side_char) {
    static const char product[] = ERA_SPLIT_USB_IDENTITY_PRODUCT_BASE;
    uint8_t           index     = 0;

    era_split_usb_identity_copy_ascii(era_split_usb_identity_product_string.UnicodeString, &index, product, ERA_SPLIT_USB_IDENTITY_PRODUCT_BASE_LENGTH);
    era_split_usb_identity_product_string.UnicodeString[index++] = '_';
    era_split_usb_identity_product_string.UnicodeString[index++] = side_char;

    era_split_usb_identity_product_string.Header.Size = sizeof(USB_Descriptor_Header_t) + (index * sizeof(uint16_t));
    era_split_usb_identity_product_string.Header.Type = DTYPE_String;
}

static void era_split_usb_identity_build_serial_string(char side_char) {
    static const char hex[]     = "0123456789ABCDEF";
    static const char product[] = ERA_SPLIT_USB_IDENTITY_PRODUCT_BASE;
    hardware_id_t     id        = get_hardware_id();
    uint8_t*          bytes     = (uint8_t*)&id;
    uint8_t           index     = 0;

    era_split_usb_identity_copy_ascii(era_split_usb_identity_serial_string.UnicodeString, &index, product, ERA_SPLIT_USB_IDENTITY_PRODUCT_BASE_LENGTH);
    era_split_usb_identity_serial_string.UnicodeString[index++] = '_';
    era_split_usb_identity_serial_string.UnicodeString[index++] = side_char;
    era_split_usb_identity_serial_string.UnicodeString[index++] = '_';

    for (uint8_t i = 0; i < sizeof(id); i++) {
        era_split_usb_identity_serial_string.UnicodeString[index++] = hex[bytes[i] >> 4];
        era_split_usb_identity_serial_string.UnicodeString[index++] = hex[bytes[i] & 0x0F];
    }

    era_split_usb_identity_serial_string.Header.Size = sizeof(USB_Descriptor_Header_t) + (index * sizeof(uint16_t));
    era_split_usb_identity_serial_string.Header.Type = DTYPE_String;
}

static void era_split_usb_identity_ensure_ready(void) {
    if (era_split_usb_identity_ready) {
        return;
    }

    bool left = split_hand_pin_is_left();
    char side = left ? 'L' : 'R';

    memcpy(&era_split_usb_identity_device_descriptor, &DeviceDescriptor, sizeof(era_split_usb_identity_device_descriptor));
    era_split_usb_identity_device_descriptor.ProductID = left ? ERA_SPLIT_USB_IDENTITY_PID_LEFT : ERA_SPLIT_USB_IDENTITY_PID_RIGHT;
    era_split_usb_identity_build_product_string(side);
    era_split_usb_identity_build_serial_string(side);
    era_split_usb_identity_ready = true;
}

void era_split_usb_identity_init(void) {
    era_split_usb_identity_ensure_ready();
}

bool usb_descriptor_device_override(const void** const DescriptorAddress, uint16_t* const DescriptorSize) {
    era_split_usb_identity_ensure_ready();
    *DescriptorAddress = &era_split_usb_identity_device_descriptor;
    *DescriptorSize    = sizeof(era_split_usb_identity_device_descriptor);
    return true;
}

bool usb_descriptor_product_string_override(const void** const DescriptorAddress, uint16_t* const DescriptorSize) {
    era_split_usb_identity_ensure_ready();
    *DescriptorAddress = &era_split_usb_identity_product_string;
    *DescriptorSize    = era_split_usb_identity_product_string.Header.Size;
    return true;
}

bool usb_descriptor_serial_number_string_override(const void** const DescriptorAddress, uint16_t* const DescriptorSize) {
    era_split_usb_identity_ensure_ready();
    *DescriptorAddress = &era_split_usb_identity_serial_string;
    *DescriptorSize    = era_split_usb_identity_serial_string.Header.Size;
    return true;
}
