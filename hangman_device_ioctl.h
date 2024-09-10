/* SPDX-License-Identifier: MIT */

#ifndef MY_IOCTL_H
#define MY_IOCTL_H

#include <linux/ioctl.h>

// Define the ioctl command number
#define IOCTL_RESET _IOW('r', 1, int)

#endif // MY_IOCTL_H
