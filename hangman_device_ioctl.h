/* SPDX-License-Identifier: MIT */

#ifndef MY_IOCTL_H
#define MY_IOCTL_H

#include <linux/ioctl.h>

// Define the ioctl command number
#define IOCTL_RESET _IO(0x07, 1)
#define IOCTL_GARBAGE _IO(0x07, 2)

#endif // MY_IOCTL_H
