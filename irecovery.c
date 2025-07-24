/*
 * irecovery.c
 * Communication to iBoot/iBSS on Apple iOS devices via USB, ported to the TI-84.
 *
 * Copyright (c) 2011-2023 Nikias Bassen <nikias@gmx.li>
 * Copyright (c) 2012-2020 Martin Szulecki <martin.szulecki@libimobiledevice.org>
 * Copyright (c) 2010 Chronic-Dev Team
 * Copyright (c) 2010 Joshua Hill
 * Copyright (c) 2008-2011 Nicolas Haunold
 * Copyright (c) 2025 Karson Eskind
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 */

#include <usbdrvce.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <sys/rtc.h>
#include <inttypes.h>
#include <sys/timers.h>
#include "irecovery.h"
#include "nsscanf.h"

#define APPLE_VENDOR_ID 0x05AC

struct irecovery_client {
    /* Static Zone - No dynamic pointers allowed */
    irecovery_connection_policy_t connection_policy; // Connection policy to use.
    irecovery_log_cb_t log_fp;                       // Log function pointer.
    uint64_t ecid_restriction;                       // Optional ECID restriction.
    int num_connections;                             // Number of connections this client has had.
      
    /* Device Zone - Anything relating to devices, anything is allowed */      
    usb_device_t handle;                             // usbdrvce handle.
    usb_device_descriptor_t device_descriptor;       // Device descriptor.
    struct irecovery_device_info device_info;        // Device info.
    unsigned int mode;                               // Device mode.
    int finalized;                                   // Whether or not this client is finalized.
	irecovery_event_cb_t progress_callback;          // Progress callback
};
#define DEVICE_ZONE_OFFSET offsetof(struct irecovery_client, handle)

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/src/libirecovery.c#L159 */
static struct irecovery_device irecovery_devices[] = {
	/* iPhone */
	{ "iPhone1,1",   "m68ap",    0x00, 0x8900, "iPhone 2G" },
	{ "iPhone1,2",   "n82ap",    0x04, 0x8900, "iPhone 3G" },
	{ "iPhone2,1",   "n88ap",    0x00, 0x8920, "iPhone 3Gs" },
	{ "iPhone3,1",   "n90ap",    0x00, 0x8930, "iPhone 4 (GSM)" },
	{ "iPhone3,2",   "n90bap",   0x04, 0x8930, "iPhone 4 (GSM) R2 2012" },
	{ "iPhone3,3",   "n92ap",    0x06, 0x8930, "iPhone 4 (CDMA)" },
	{ "iPhone4,1",   "n94ap",    0x08, 0x8940, "iPhone 4s" },
	{ "iPhone5,1",   "n41ap",    0x00, 0x8950, "iPhone 5 (GSM)" },
	{ "iPhone5,2",   "n42ap",    0x02, 0x8950, "iPhone 5 (Global)" },
	{ "iPhone5,3",   "n48ap",    0x0a, 0x8950, "iPhone 5c (GSM)" },
	{ "iPhone5,4",   "n49ap",    0x0e, 0x8950, "iPhone 5c (Global)" },
	{ "iPhone6,1",   "n51ap",    0x00, 0x8960, "iPhone 5s (GSM)" },
	{ "iPhone6,2",   "n53ap",    0x02, 0x8960, "iPhone 5s (Global)" },
	{ "iPhone7,1",   "n56ap",    0x04, 0x7000, "iPhone 6 Plus" },
	{ "iPhone7,2",   "n61ap",    0x06, 0x7000, "iPhone 6" },
	{ "iPhone8,1",   "n71ap",    0x04, 0x8000, "iPhone 6s" },
	{ "iPhone8,1",   "n71map",   0x04, 0x8003, "iPhone 6s" },
	{ "iPhone8,2",   "n66ap",    0x06, 0x8000, "iPhone 6s Plus" },
	{ "iPhone8,2",   "n66map",   0x06, 0x8003, "iPhone 6s Plus" },
	{ "iPhone8,4",   "n69ap",    0x02, 0x8003, "iPhone SE (1st gen)" },
	{ "iPhone8,4",   "n69uap",   0x02, 0x8000, "iPhone SE (1st gen)" },
	{ "iPhone9,1",   "d10ap",    0x08, 0x8010, "iPhone 7 (Global)" },
	{ "iPhone9,2",   "d11ap",    0x0a, 0x8010, "iPhone 7 Plus (Global)" },
	{ "iPhone9,3",   "d101ap",   0x0c, 0x8010, "iPhone 7 (GSM)" },
	{ "iPhone9,4",   "d111ap",   0x0e, 0x8010, "iPhone 7 Plus (GSM)" },
	{ "iPhone10,1",  "d20ap",    0x02, 0x8015, "iPhone 8 (Global)" },
	{ "iPhone10,2",  "d21ap",    0x04, 0x8015, "iPhone 8 Plus (Global)" },
	{ "iPhone10,3",  "d22ap",    0x06, 0x8015, "iPhone X (Global)" },
	{ "iPhone10,4",  "d201ap",   0x0a, 0x8015, "iPhone 8 (GSM)" },
	{ "iPhone10,5",  "d211ap",   0x0c, 0x8015, "iPhone 8 Plus (GSM)" },
	{ "iPhone10,6",  "d221ap",   0x0e, 0x8015, "iPhone X (GSM)" },
	{ "iPhone11,2",  "d321ap",   0x0e, 0x8020, "iPhone XS" },
	{ "iPhone11,4",  "d331ap",   0x0a, 0x8020, "iPhone XS Max (China)" },
	{ "iPhone11,6",  "d331pap",  0x1a, 0x8020, "iPhone XS Max" },
	{ "iPhone11,8",  "n841ap",   0x0c, 0x8020, "iPhone XR" },
	{ "iPhone12,1",  "n104ap",   0x04, 0x8030, "iPhone 11" },
	{ "iPhone12,3",  "d421ap",   0x06, 0x8030, "iPhone 11 Pro" },
	{ "iPhone12,5",  "d431ap",   0x02, 0x8030, "iPhone 11 Pro Max" },
	{ "iPhone12,8",  "d79ap",    0x10, 0x8030, "iPhone SE (2nd gen)" },
	{ "iPhone13,1",  "d52gap",   0x0A, 0x8101, "iPhone 12 mini" },
	{ "iPhone13,2",  "d53gap",   0x0C, 0x8101, "iPhone 12" },
	{ "iPhone13,3",  "d53pap",   0x0E, 0x8101, "iPhone 12 Pro" },
	{ "iPhone13,4",  "d54pap",   0x08, 0x8101, "iPhone 12 Pro Max" },
	{ "iPhone14,2",  "d63ap",    0x0C, 0x8110, "iPhone 13 Pro" },
	{ "iPhone14,3",  "d64ap",    0x0E, 0x8110, "iPhone 13 Pro Max" },
	{ "iPhone14,4",  "d16ap",    0x08, 0x8110, "iPhone 13 mini" },
	{ "iPhone14,5",  "d17ap",    0x0A, 0x8110, "iPhone 13" },
	{ "iPhone14,6",  "d49ap",    0x10, 0x8110, "iPhone SE (3rd gen)" },
	{ "iPhone14,7",	 "d27ap",    0x18, 0x8110, "iPhone 14" },
	{ "iPhone14,8",	 "d28ap",    0x1A, 0x8110, "iPhone 14 Plus" },
	{ "iPhone15,2",	 "d73ap",    0x0C, 0x8120, "iPhone 14 Pro" },
	{ "iPhone15,3",	 "d74ap",    0x0E, 0x8120, "iPhone 14 Pro Max" },
	{ "iPhone15,4",	 "d37ap",    0x08, 0x8120, "iPhone 15" },
	{ "iPhone15,5",	 "d38ap",    0x0A, 0x8120, "iPhone 15 Plus" },
	{ "iPhone16,1",	 "d83ap",    0x04, 0x8130, "iPhone 15 Pro" },
	{ "iPhone16,2",	 "d84ap",    0x06, 0x8130, "iPhone 15 Pro Max" },
	{ "iPhone17,1",	 "d93ap",    0x0C, 0x8140, "iPhone 16 Pro" },
	{ "iPhone17,2",	 "d94ap",    0x0E, 0x8140, "iPhone 16 Pro Max" },
	{ "iPhone17,3",	 "d47ap",    0x08, 0x8140, "iPhone 16" },
	{ "iPhone17,4",	 "d48ap",    0x0A, 0x8140, "iPhone 16 Plus" },
	{ "iPhone17,5",	 "v59ap",    0x04, 0x8140, "iPhone 16e" },
	/* iPod */
	{ "iPod1,1",     "n45ap",    0x02, 0x8900, "iPod Touch (1st gen)" },
	{ "iPod2,1",     "n72ap",    0x00, 0x8720, "iPod Touch (2nd gen)" },
	{ "iPod3,1",     "n18ap",    0x02, 0x8922, "iPod Touch (3rd gen)" },
	{ "iPod4,1",     "n81ap",    0x08, 0x8930, "iPod Touch (4th gen)" },
	{ "iPod5,1",     "n78ap",    0x00, 0x8942, "iPod Touch (5th gen)" },
	{ "iPod7,1",     "n102ap",   0x10, 0x7000, "iPod Touch (6th gen)" },
	{ "iPod9,1",     "n112ap",   0x16, 0x8010, "iPod Touch (7th gen)" },
	/* iPad */
	{ "iPad1,1",     "k48ap",    0x02, 0x8930, "iPad" },
	{ "iPad2,1",     "k93ap",    0x04, 0x8940, "iPad 2 (WiFi)" },
	{ "iPad2,2",     "k94ap",    0x06, 0x8940, "iPad 2 (GSM)" },
	{ "iPad2,3",     "k95ap",    0x02, 0x8940, "iPad 2 (CDMA)" },
	{ "iPad2,4",     "k93aap",   0x06, 0x8942, "iPad 2 (WiFi) R2 2012" },
	{ "iPad2,5",     "p105ap",   0x0a, 0x8942, "iPad mini (WiFi)" },
	{ "iPad2,6",     "p106ap",   0x0c, 0x8942, "iPad mini (GSM)" },
	{ "iPad2,7",     "p107ap",   0x0e, 0x8942, "iPad mini (Global)" },
	{ "iPad3,1",     "j1ap",     0x00, 0x8945, "iPad (3rd gen, WiFi)" },
	{ "iPad3,2",     "j2ap",     0x02, 0x8945, "iPad (3rd gen, CDMA)" },
	{ "iPad3,3",     "j2aap",    0x04, 0x8945, "iPad (3rd gen, GSM)" },
	{ "iPad3,4",     "p101ap",   0x00, 0x8955, "iPad (4th gen, WiFi)" },
	{ "iPad3,5",     "p102ap",   0x02, 0x8955, "iPad (4th gen, GSM)" },
	{ "iPad3,6",     "p103ap",   0x04, 0x8955, "iPad (4th gen, Global)" },
	{ "iPad4,1",     "j71ap",    0x10, 0x8960, "iPad Air (WiFi)" },
	{ "iPad4,2",     "j72ap",    0x12, 0x8960, "iPad Air (Cellular)" },
	{ "iPad4,3",     "j73ap",    0x14, 0x8960, "iPad Air (China)" },
	{ "iPad4,4",     "j85ap",    0x0a, 0x8960, "iPad mini 2 (WiFi)" },
	{ "iPad4,5",     "j86ap",    0x0c, 0x8960, "iPad mini 2 (Cellular)" },
	{ "iPad4,6",     "j87ap",    0x0e, 0x8960, "iPad mini 2 (China)" },
	{ "iPad4,7",     "j85map",   0x32, 0x8960, "iPad mini 3 (WiFi)" },
	{ "iPad4,8",     "j86map",   0x34, 0x8960, "iPad mini 3 (Cellular)" },
	{ "iPad4,9",     "j87map",   0x36, 0x8960, "iPad mini 3 (China)" },
	{ "iPad5,1",     "j96ap",    0x08, 0x7000, "iPad mini 4 (WiFi)" },
	{ "iPad5,2",     "j97ap",    0x0A, 0x7000, "iPad mini 4 (Cellular)" },
	{ "iPad5,3",     "j81ap",    0x06, 0x7001, "iPad Air 2 (WiFi)" },
	{ "iPad5,4",     "j82ap",    0x02, 0x7001, "iPad Air 2 (Cellular)" },
	{ "iPad6,3",     "j127ap",   0x08, 0x8001, "iPad Pro 9.7-inch (WiFi)" },
	{ "iPad6,4",     "j128ap",   0x0a, 0x8001, "iPad Pro 9.7-inch (Cellular)" },
	{ "iPad6,7",     "j98aap",   0x10, 0x8001, "iPad Pro 12.9-inch (1st gen, WiFi)" },
	{ "iPad6,8",     "j99aap",   0x12, 0x8001, "iPad Pro 12.9-inch (1st gen, Cellular)" },
	{ "iPad6,11",    "j71sap",   0x10, 0x8000, "iPad (5th gen, WiFi)" },
	{ "iPad6,11",    "j71tap",   0x10, 0x8003, "iPad (5th gen, WiFi)" },
	{ "iPad6,12",    "j72sap",   0x12, 0x8000, "iPad (5th gen, Cellular)" },
	{ "iPad6,12",    "j72tap",   0x12, 0x8003, "iPad (5th gen, Cellular)" },
	{ "iPad7,1",     "j120ap",   0x0C, 0x8011, "iPad Pro 12.9-inch (2nd gen, WiFi)" },
	{ "iPad7,2",     "j121ap",   0x0E, 0x8011, "iPad Pro 12.9-inch (2nd gen, Cellular)" },
	{ "iPad7,3",     "j207ap",   0x04, 0x8011, "iPad Pro 10.5-inch (WiFi)" },
	{ "iPad7,4",     "j208ap",   0x06, 0x8011, "iPad Pro 10.5-inch (Cellular)" },
	{ "iPad7,5",     "j71bap",   0x18, 0x8010, "iPad (6th gen, WiFi)" },
	{ "iPad7,6",     "j72bap",   0x1A, 0x8010, "iPad (6th gen, Cellular)" },
	{ "iPad7,11",    "j171ap",   0x1C, 0x8010, "iPad (7th gen, WiFi)" },
	{ "iPad7,12",    "j172ap",   0x1E, 0x8010, "iPad (7th gen, Cellular)" },
	{ "iPad8,1",     "j317ap",   0x0C, 0x8027, "iPad Pro 11-inch (1st gen, WiFi)" },
	{ "iPad8,2",     "j317xap",  0x1C, 0x8027, "iPad Pro 11-inch (1st gen, WiFi, 1TB)" },
	{ "iPad8,3",     "j318ap",   0x0E, 0x8027, "iPad Pro 11-inch (1st gen, Cellular)" },
	{ "iPad8,4",     "j318xap",  0x1E, 0x8027, "iPad Pro 11-inch (1st gen, Cellular, 1TB)" },
	{ "iPad8,5",     "j320ap",   0x08, 0x8027, "iPad Pro 12.9-inch (3rd gen, WiFi)" },
	{ "iPad8,6",     "j320xap",  0x18, 0x8027, "iPad Pro 12.9-inch (3rd gen, WiFi, 1TB)" },
	{ "iPad8,7",     "j321ap",   0x0A, 0x8027, "iPad Pro 12.9-inch (3rd gen, Cellular)" },
	{ "iPad8,8",     "j321xap",  0x1A, 0x8027, "iPad Pro 12.9-inch (3rd gen, Cellular, 1TB)" },
	{ "iPad8,9",     "j417ap",   0x3C, 0x8027, "iPad Pro 11-inch (2nd gen, WiFi)" },
	{ "iPad8,10",    "j418ap",   0x3E, 0x8027, "iPad Pro 11-inch (2nd gen, Cellular)" },
	{ "iPad8,11",    "j420ap",   0x38, 0x8027, "iPad Pro 12.9-inch (4th gen, WiFi)" },
	{ "iPad8,12",    "j421ap",   0x3A, 0x8027, "iPad Pro 12.9-inch (4th gen, Cellular)" },
	{ "iPad11,1",    "j210ap",   0x14, 0x8020, "iPad mini (5th gen, WiFi)" },
	{ "iPad11,2",    "j211ap",   0x16, 0x8020, "iPad mini (5th gen, Cellular)" },
	{ "iPad11,3",    "j217ap",   0x1C, 0x8020, "iPad Air (3rd gen, WiFi)" },
	{ "iPad11,4",    "j218ap",   0x1E, 0x8020, "iPad Air (3rd gen, Cellular)" },
	{ "iPad11,6",    "j171aap",  0x24, 0x8020, "iPad (8th gen, WiFi)" },
	{ "iPad11,7",    "j172aap",  0x26, 0x8020, "iPad (8th gen, Cellular)" },
	{ "iPad12,1",    "j181ap",   0x18, 0x8030, "iPad (9th gen, WiFi)" },
	{ "iPad12,2",    "j182ap",   0x1A, 0x8030, "iPad (9th gen, Cellular)" },
	{ "iPad13,1",    "j307ap",   0x04, 0x8101, "iPad Air (4th gen, WiFi)" },
	{ "iPad13,2",    "j308ap",   0x06, 0x8101, "iPad Air (4th gen, Cellular)" },
	{ "iPad13,4",    "j517ap",   0x08, 0x8103, "iPad Pro 11-inch (3rd gen, WiFi)" },
	{ "iPad13,5",    "j517xap",  0x0A, 0x8103, "iPad Pro 11-inch (3rd gen, WiFi, 2TB)" },
	{ "iPad13,6",    "j518ap",   0x0C, 0x8103, "iPad Pro 11-inch (3rd gen, Cellular)" },
	{ "iPad13,7",    "j518xap",  0x0E, 0x8103, "iPad Pro 11-inch (3rd gen, Cellular, 2TB)" },
	{ "iPad13,8",    "j522ap",   0x18, 0x8103, "iPad Pro 12.9-inch (5th gen, WiFi)" },
	{ "iPad13,9",    "j522xap",  0x1A, 0x8103, "iPad Pro 12.9-inch (5th gen, WiFi, 2TB)" },
	{ "iPad13,10",   "j523ap",   0x1C, 0x8103, "iPad Pro 12.9-inch (5th gen, Cellular)" },
	{ "iPad13,11",   "j523xap",  0x1E, 0x8103, "iPad Pro 12.9-inch (5th gen, Cellular, 2TB)" },
	{ "iPad13,16",   "j407ap",   0x10, 0x8103, "iPad Air (5th gen, WiFi)" },
	{ "iPad13,17",   "j408ap",   0x12, 0x8103, "iPad Air (5th gen, Cellular)" },
	{ "iPad13,18",   "j271ap",   0x14, 0x8101, "iPad (10th gen, WiFi)" },
	{ "iPad13,19",   "j272ap",   0x16, 0x8101, "iPad (10th gen, Cellular)" },
	{ "iPad14,1",    "j310ap",   0x04, 0x8110, "iPad mini (6th gen, WiFi)" },
	{ "iPad14,2",    "j311ap",   0x06, 0x8110, "iPad mini (6th gen, Cellular)" },
	{ "iPad14,3",    "j617ap",   0x08, 0x8112, "iPad Pro 11-inch (4th gen, WiFi)" },
	{ "iPad14,4",    "j618ap",   0x0A, 0x8112, "iPad Pro 11-inch (4th gen, Cellular)" },
	{ "iPad14,5",    "j620ap",   0x0C, 0x8112, "iPad Pro 12.9-inch (6th gen, WiFi)" },
	{ "iPad14,6",    "j621ap",   0x0E, 0x8112, "iPad Pro 12.9-inch (6th gen, Cellular)" },
	{ "iPad14,8",    "j507ap",   0x10, 0x8112, "iPad Air 11-inch (M2, WiFi)" },
	{ "iPad14,9",    "j508ap",   0x12, 0x8112, "iPad Air 11-inch (M2, Cellular)" },
	{ "iPad14,10",   "j537ap",   0x14, 0x8112, "iPad Air 13-inch (M2, WiFi)" },
	{ "iPad14,11",   "j538ap",   0x16, 0x8112, "iPad Air 13-inch (M2, Cellular)" },
	{ "iPad15,3",    "j607ap",   0x08, 0x8122, "iPad Air 11-inch (M3, WiFi)" },
	{ "iPad15,4",    "j608ap",   0x0A, 0x8122, "iPad Air 11-inch (M3, Cellular)" },
	{ "iPad15,5",    "j637ap",   0x0C, 0x8122, "iPad Air 13-inch (M3, WiFi)" },
	{ "iPad15,6",    "j638ap",   0x0E, 0x8122, "iPad Air 13-inch (M3, Cellular)" },
	{ "iPad15,7",    "j481ap",   0x10, 0x8120, "iPad (A16, WiFi)" },
	{ "iPad15,8",    "j482ap",   0x12, 0x8120, "iPad (A16, Cellular)" },
	{ "iPad16,1",    "j410ap",   0x08, 0x8130, "iPad mini (A17 Pro, WiFi)" },
	{ "iPad16,2",    "j411ap",   0x0A, 0x8130, "iPad mini (A17 Pro, Cellular)" },
	{ "iPad16,3",    "j717ap",   0x08, 0x8132, "iPad Pro 11-inch (M4, WiFi)" },
	{ "iPad16,4",    "j718ap",   0x0A, 0x8132, "iPad Pro 11-inch (M4, Cellular)" },
	{ "iPad16,5",    "j720ap",   0x0C, 0x8132, "iPad Pro 13-inch (M4, WiFi)" },
	{ "iPad16,6",    "j721ap",   0x0E, 0x8132, "iPad Pro 13-inch (M4, Cellular)" },
	/* Apple TV */
	{ "AppleTV2,1",  "k66ap",    0x10, 0x8930, "Apple TV 2" },
	{ "AppleTV3,1",  "j33ap",    0x08, 0x8942, "Apple TV 3" },
	{ "AppleTV3,2",  "j33iap",   0x00, 0x8947, "Apple TV 3 (2013)" },
	{ "AppleTV5,3",  "j42dap",   0x34, 0x7000, "Apple TV 4" },
	{ "AppleTV6,2",  "j105aap",  0x02, 0x8011, "Apple TV 4K" },
	{ "AppleTV11,1", "j305ap",   0x08, 0x8020, "Apple TV 4K (2nd gen)" },
	{ "AppleTV14,1", "j255ap",   0x02, 0x8110, "Apple TV 4K (3rd gen)" },
	/* HomePod */
	{ "AudioAccessory1,1",  "b238aap",  0x38, 0x7000, "HomePod (1st gen)" },
	{ "AudioAccessory1,2",  "b238ap",   0x1A, 0x7000, "HomePod (1st gen)" },
	{ "AudioAccessory5,1",  "b520ap",   0x22, 0x8006, "HomePod mini" },
	{ "AudioAccessory6,1",  "b620ap",   0x18, 0x8301, "HomePod (2nd gen)" },
	/* Apple Watch */
	{ "Watch1,1",    "n27aap",   0x02, 0x7002, "Apple Watch 38mm (1st gen)" },
	{ "Watch1,2",    "n28aap",   0x04, 0x7002, "Apple Watch 42mm (1st gen)" },
	{ "Watch2,6",    "n27dap",  0x02, 0x8002, "Apple Watch Series 1 (38mm)" },
	{ "Watch2,7",    "n28dap",  0x04, 0x8002, "Apple Watch Series 1 (42mm)" },
	{ "Watch2,3",    "n74ap",   0x0C, 0x8002, "Apple Watch Series 2 (38mm)" },
	{ "Watch2,4",    "n75ap",   0x0E, 0x8002, "Apple Watch Series 2 (42mm)" },
	{ "Watch3,1",    "n111sap", 0x1C, 0x8004, "Apple Watch Series 3 (38mm Cellular)" },
	{ "Watch3,2",    "n111bap", 0x1E, 0x8004, "Apple Watch Series 3 (42mm Cellular)" },
	{ "Watch3,3",    "n121sap", 0x18, 0x8004, "Apple Watch Series 3 (38mm)" },
	{ "Watch3,4",    "n121bap", 0x1A, 0x8004, "Apple Watch Series 3 (42mm)" },
	{ "Watch4,1",    "n131sap", 0x08, 0x8006, "Apple Watch Series 4 (40mm)" },
	{ "Watch4,2",    "n131bap", 0x0A, 0x8006, "Apple Watch Series 4 (44mm)" },
	{ "Watch4,3",    "n141sap", 0x0C, 0x8006, "Apple Watch Series 4 (40mm Cellular)" },
	{ "Watch4,4",    "n141bap", 0x0E, 0x8006, "Apple Watch Series 4 (44mm Cellular)" },
	{ "Watch5,1",    "n144sap", 0x10, 0x8006, "Apple Watch Series 5 (40mm)" },
	{ "Watch5,2",    "n144bap", 0x12, 0x8006, "Apple Watch Series 5 (44mm)" },
	{ "Watch5,3",    "n146sap", 0x14, 0x8006, "Apple Watch Series 5 (40mm Cellular)" },
	{ "Watch5,4",    "n146bap", 0x16, 0x8006, "Apple Watch Series 5 (44mm Cellular)" },
	{ "Watch5,9",    "n140sap", 0x28, 0x8006, "Apple Watch SE (40mm)" },
	{ "Watch5,10",   "n140bap", 0x2A, 0x8006, "Apple Watch SE (44mm)" },
	{ "Watch5,11",   "n142sap", 0x2C, 0x8006, "Apple Watch SE (40mm Cellular)" },
	{ "Watch5,12",   "n142bap", 0x2E, 0x8006, "Apple Watch SE (44mm Cellular)" },
	{ "Watch6,1",    "n157sap", 0x08, 0x8301, "Apple Watch Series 6 (40mm)" },
	{ "Watch6,2",    "n157bap", 0x0A, 0x8301, "Apple Watch Series 6 (44mm)" },
	{ "Watch6,3",    "n158sap", 0x0C, 0x8301, "Apple Watch Series 6 (40mm Cellular)" },
	{ "Watch6,4",    "n158bap", 0x0E, 0x8301, "Apple Watch Series 6 (44mm Cellular)" },
	{ "Watch6,6",    "n187sap", 0x10, 0x8301, "Apple Watch Series 7 (41mm)" },
	{ "Watch6,7",    "n187bap", 0x12, 0x8301, "Apple Watch Series 7 (45mm)" },
	{ "Watch6,8",    "n188sap", 0x14, 0x8301, "Apple Watch Series 7 (41mm Cellular)" },
	{ "Watch6,9",    "n188bap", 0x16, 0x8301, "Apple Watch Series 7 (45mm Cellular)" },
	{ "Watch6,10",   "n143sap", 0x28, 0x8301, "Apple Watch SE 2 (40mm)" },
	{ "Watch6,11",   "n143bap", 0x2A, 0x8301, "Apple Watch SE 2 (44mm)" },
	{ "Watch6,12",   "n149sap", 0x2C, 0x8301, "Apple Watch SE 2 (40mm Cellular)" },
	{ "Watch6,13",   "n149bap", 0x2E, 0x8301, "Apple Watch SE 2 (44mm Cellular)" },
	{ "Watch6,14",   "n197sap", 0x30, 0x8301, "Apple Watch Series 8 (41mm)" },
	{ "Watch6,15",   "n197bap", 0x32, 0x8301, "Apple Watch Series 8 (45mm)" },
	{ "Watch6,16",   "n198sap", 0x34, 0x8301, "Apple Watch Series 8 (41mm Cellular)" },
	{ "Watch6,17",   "n198bap", 0x36, 0x8301, "Apple Watch Series 8 (45mm Cellular)" },
	{ "Watch6,18",   "n199ap",  0x26, 0x8301, "Apple Watch Ultra" },
	{ "Watch7,1",    "n207sap", 0x08, 0x8310, "Apple Watch Series 9 (41mm)" },
	{ "Watch7,2",    "n207bap", 0x0A, 0x8310, "Apple Watch Series 9 (45mm)" },
	{ "Watch7,3",    "n208sap", 0x0C, 0x8310, "Apple Watch Series 9 (41mm Cellular)" },
	{ "Watch7,4",    "n208bap", 0x0E, 0x8310, "Apple Watch Series 9 (45mm Cellular)" },
	{ "Watch7,5",    "n210ap",  0x02, 0x8310, "Apple Watch Ultra 2" },
	{ "Watch7,8",    "n217sap", 0x10, 0x8310, "Apple Watch Series 10 (42mm)" },
	{ "Watch7,9",    "n217bap", 0x12, 0x8310, "Apple Watch Series 10 (46mm)" },
	{ "Watch7,10",   "n218sap", 0x14, 0x8310, "Apple Watch Series 10 (42mm Cellular)" },
	{ "Watch7,11",   "n218bap", 0x16, 0x8310, "Apple Watch Series 10 (46mm Cellular)" },
	/* Apple Silicon Macs */
	{ "ADP3,2",         "j273aap", 0x42, 0x8027, "Developer Transition Kit (2020)" },
	{ "Macmini9,1",	    "j274ap",  0x22, 0x8103, "Mac mini (M1, 2020)" },
	{ "MacBookPro17,1", "j293ap",  0x24, 0x8103, "MacBook Pro (M1, 13-inch, 2020)" },
	{ "MacBookPro18,1", "j316sap", 0x0A, 0x6000, "MacBook Pro (M1 Pro, 16-inch, 2021)" },
	{ "MacBookPro18,2", "j316cap", 0x0A, 0x6001, "MacBook Pro (M1 Max, 16-inch, 2021)" },
	{ "MacBookPro18,3", "j314sap", 0x08, 0x6000, "MacBook Pro (M1 Pro, 14-inch, 2021)" },
	{ "MacBookPro18,4", "j314cap", 0x08, 0x6001, "MacBook Pro (M1 Max, 14-inch, 2021)" },
	{ "MacBookAir10,1", "j313ap",  0x26, 0x8103, "MacBook Air (M1, 2020)" },
	{ "iMac21,1",       "j456ap",  0x28, 0x8103, "iMac 24-inch (M1, Two Ports, 2021)" },
	{ "iMac21,2",       "j457ap",  0x2A, 0x8103, "iMac 24-inch (M1, Four Ports, 2021)" },
	{ "Mac13,1",        "j375cap", 0x04, 0x6001, "Mac Studio (M1 Max, 2022)" },
	{ "Mac13,2",        "j375dap", 0x0C, 0x6002, "Mac Studio (M1 Ultra, 2022)" },
	{ "Mac14,2",        "j413ap",  0x28, 0x8112, "MacBook Air (M2, 2022)" },
	{ "Mac14,7",        "j493ap",  0x2A, 0x8112, "MacBook Pro (M2, 13-inch, 2022)" },
	{ "Mac14,3",        "j473ap",  0x24, 0x8112, "Mac mini (M2, 2023)" },
	{ "Mac14,5",        "j414cap", 0x04, 0x6021, "MacBook Pro (14-inch, M2 Max, 2023)" },
	{ "Mac14,6",        "j416cap", 0x06, 0x6021, "MacBook Pro (16-inch, M2 Max, 2023)" },
	{ "Mac14,8",        "j180dap", 0x08, 0x6022, "Mac Pro (2023)" },
	{ "Mac14,9",        "j414sap", 0x04, 0x6020, "MacBook Pro (14-inch, M2 Pro, 2023)" },
	{ "Mac14,10",       "j416sap", 0x06, 0x6020, "MacBook Pro (16-inch, M2 Pro, 2023)" },
	{ "Mac14,12",       "j474sap", 0x02, 0x6020, "Mac mini (M2 Pro, 2023)" },
	{ "Mac14,13",       "j475cap", 0x0A, 0x6021, "Mac Studio (M2 Max, 2023)" },
	{ "Mac14,14",       "j475dap", 0x0A, 0x6022, "Mac Studio (M2 Ultra, 2023)" },
	{ "Mac14,15",       "j415ap",  0x2E, 0x8112, "MacBook Air (M2, 15-inch, 2023)" },
	{ "Mac15,3",        "j504ap",  0x22, 0x8122, "MacBook Pro (14-inch, M3, Nov 2023)" },
	{ "Mac15,4",        "j433ap",  0x28, 0x8122, "iMac 24-inch (M3, Two Ports, 2023)" },
	{ "Mac15,5",        "j434ap",  0x2A, 0x8122, "iMac 24-inch (M3, Four Ports, 2023)" },
	{ "Mac15,6",        "j514sap", 0x04, 0x6030, "MacBook Pro (14-inch, M3 Pro, Nov 2023)" },
	{ "Mac15,7",        "j516sap", 0x06, 0x6030, "MacBook Pro (16-inch, M3 Pro, Nov 2023)" },
	{ "Mac15,8",        "j514cap", 0x44, 0x6031, "MacBook Pro (14-inch, M3 Max, Nov 2023)" },
	{ "Mac15,9",        "j516cap", 0x46, 0x6031, "MacBook Pro (16-inch, M3 Max, Nov 2023)" },
	{ "Mac15,10",       "j514map", 0x44, 0x6034, "MacBook Pro (14-inch, M3 Max, Nov 2023)" },
	{ "Mac15,11",       "j516map", 0x46, 0x6034, "MacBook Pro (16-inch, M3 Max, Nov 2023)" },
	{ "Mac15,12",       "j613ap",  0x30, 0x8122, "MacBook Air (13-inch, M3, 2024)" },
	{ "Mac15,13",       "j615ap",  0x32, 0x8122, "MacBook Air (15-inch, M3, 2024)" },
	{ "Mac15,14",       "j575dap", 0x44, 0x6032, "Mac Studio (M3 Ultra, 2025)" },
	{ "Mac16,1",        "j604ap",  0x22, 0x8132, "MacBook Pro (14-inch, M4, Nov 2024)" },
	{ "Mac16,2",        "j623ap",  0x24, 0x8132, "iMac 24-inch (M4, Two Ports, 2024)" },
	{ "Mac16,3",        "j624ap",  0x26, 0x8132, "iMac 24-inch (M4, Four Ports, 2024)" },
	{ "Mac16,5",        "j616cap", 0x06, 0x6041, "MacBook Pro (16-inch, M4 Max, Nov 2024)" },
	{ "Mac16,6",        "j614cap", 0x04, 0x6041, "MacBook Pro (14-inch, M4 Max, Nov 2024)" },
	{ "Mac16,7",        "j616sap", 0x06, 0x6040, "MacBook Pro (16-inch, M4 Pro, Nov 2024)" },
	{ "Mac16,8",        "j614sap", 0x04, 0x6040, "MacBook Pro (14-inch, M4 Pro, Nov 2024)" },
	{ "Mac16,9",        "j575cap", 0x02, 0x6041, "Mac Studio (M4 Max, 2025)" },
	{ "Mac16,10",       "j773gap", 0x2A, 0x8132, "Mac mini (M4, 2024)" },
	{ "Mac16,11",       "j773sap", 0x02, 0x6040, "Mac mini (M4 Pro, 2024)" },
	{ "Mac16,12",       "j713ap",  0x2C, 0x8132, "MacBook Air (13-inch, M4, 2025)" },
	{ "Mac16,13",       "j715ap",  0x2E, 0x8132, "MacBook Air (15-inch, M4, 2025)" },
	/* Apple Silicon VMs (supported by Virtualization.framework on macOS 12) */
	{ "VirtualMac2,1",  "vma2macosap",  0x20, 0xFE00, "Apple Virtual Machine 1" },
	/* Apple T2 Coprocessor */
	{ "iBridge2,1",	 "j137ap",   0x0A, 0x8012, "Apple T2 iMacPro1,1 (j137)" },
	{ "iBridge2,3",	 "j680ap",   0x0B, 0x8012, "Apple T2 MacBookPro15,1 (j680)" },
	{ "iBridge2,4",	 "j132ap",   0x0C, 0x8012, "Apple T2 MacBookPro15,2 (j132)" },
	{ "iBridge2,5",	 "j174ap",   0x0E, 0x8012, "Apple T2 Macmini8,1 (j174)" },
	{ "iBridge2,6",	 "j160ap",   0x0F, 0x8012, "Apple T2 MacPro7,1 (j160)" },
	{ "iBridge2,7",	 "j780ap",   0x07, 0x8012, "Apple T2 MacBookPro15,3 (j780)" },
	{ "iBridge2,8",	 "j140kap",  0x17, 0x8012, "Apple T2 MacBookAir8,1 (j140k)" },
	{ "iBridge2,10", "j213ap",   0x18, 0x8012, "Apple T2 MacBookPro15,4 (j213)" },
	{ "iBridge2,12", "j140aap",  0x37, 0x8012, "Apple T2 MacBookAir8,2 (j140a)" },
	{ "iBridge2,14", "j152fap",  0x3A, 0x8012, "Apple T2 MacBookPro16,1 (j152f)" },
	{ "iBridge2,15", "j230kap",  0x3F, 0x8012, "Apple T2 MacBookAir9,1 (j230k)" },
	{ "iBridge2,16", "j214kap",  0x3E, 0x8012, "Apple T2 MacBookPro16,2 (j214k)" },
	{ "iBridge2,19", "j185ap",   0x22, 0x8012, "Apple T2 iMac20,1 (j185)" },
	{ "iBridge2,20", "j185fap",  0x23, 0x8012, "Apple T2 iMac20,2 (j185f)" },
	{ "iBridge2,21", "j223ap",   0x3B, 0x8012, "Apple T2 MacBookPro16,3 (j223)" },
	{ "iBridge2,22", "j215ap",   0x38, 0x8012, "Apple T2 MacBookPro16,4 (j215)" },
	/* Apple Displays */
	{ "AppleDisplay2,1", "j327ap", 0x22, 0x8030, "Studio Display" },
	/* Apple Vision Pro */
	{ "RealityDevice14,1", "n301ap", 0x42, 0x8112, "Apple Vision Pro" },
	{ NULL,          NULL,         -1,     -1, NULL }
};

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/src/libirecovery.c#L468 */
static uint32_t crc32_lookup_t1[256] = {
	0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
	0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
	0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
	0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
	0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE,
	0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
	0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
	0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
	0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
	0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
	0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940,
	0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
	0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116,
	0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
	0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
	0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
	0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A,
	0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
	0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818,
	0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
	0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
	0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
	0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C,
	0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
	0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2,
	0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
	0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
	0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
	0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086,
	0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
	0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4,
	0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
	0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
	0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
	0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
	0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
	0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE,
	0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
	0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
	0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
	0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252,
	0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
	0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60,
	0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
	0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
	0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
	0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04,
	0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
	0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
	0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
	0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
	0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
	0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E,
	0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
	0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C,
	0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
	0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
	0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
	0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0,
	0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
	0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6,
	0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
	0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
	0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D,
};

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/src/libirecovery.c#L535 */
#define crc32_step(a,b) \
	a = (crc32_lookup_t1[(a & 0xFF) ^ ((unsigned char)b)] ^ (a >> 8))

void irecovery_log(irecovery_client_t client, const char* fmt, ...) {
    if (!client || !client->log_fp) return;

    size_t buffer_len = 256; // Reasonable default size for most messages
    char* buffer = (char*)malloc(buffer_len);
    if (!buffer) return;

    va_list args;
    va_start(args, fmt);
    int needed = vsnprintf(buffer, buffer_len, fmt, args);
    va_end(args);

    // If the buffer wasn't large enough, reallocate
    if (needed < 0 || (size_t)needed >= buffer_len) {
        char* bigger = (char*)realloc(buffer, needed + 1);
        if (!bigger) {
            free(buffer);
            return;
        }
        buffer = bigger;

        va_start(args, fmt);
        vsnprintf(buffer, needed + 1, fmt, args);
        va_end(args);
    }

    for (size_t i = 0; buffer[i] != '\0'; i++) {
        client->log_fp(buffer[i]);
    }

    free(buffer);
}

static bool device_zone_nonzero(irecovery_client_t client) {
    if (!client) return false;

    uint8_t* zone = (uint8_t*)client + DEVICE_ZONE_OFFSET;
    size_t len = sizeof(struct irecovery_client) - DEVICE_ZONE_OFFSET;

    for (size_t i = 0; i < len; i++) {
        if (zone[i] != 0) return true;
    }

    return false;
}

void irecovery_client_clear_device_zone(irecovery_client_t client) {
    if (!client || !device_zone_nonzero(client)) return;

    // Free dynamically allocated fields if needed
    free(client->device_info.srnm);
    free(client->device_info.imei);
    free(client->device_info.srtg);
    free(client->device_info.serial_string);
	free(client->device_info.pwnd);
    free(client->device_info.ap_nonce);
    free(client->device_info.sep_nonce);

    // Zero out the Device Zone
    memset((uint8_t*)client + DEVICE_ZONE_OFFSET, 0, sizeof(struct irecovery_client) - DEVICE_ZONE_OFFSET);

    irecovery_log(client, "Device Zone @ %p was cleared.\n", (void*)client);
}

// Note: ret_device_descriptor can be NULL.
static bool irecovery_device_is_supported(usb_device_t device, usb_device_descriptor_t* ret_device_descriptor) {
    if (!device) return false;
    
    // Get the device descriptor
    usb_device_descriptor_t device_descriptor;
    size_t transferred = 0;
    usb_error_t error = usb_GetDescriptor(device, USB_DEVICE_DESCRIPTOR, 0, &device_descriptor, sizeof(device_descriptor), &transferred);
    if (error != USB_SUCCESS || transferred != sizeof(device_descriptor)) return false;

    // Compare against the VID and PID
    bool is_supported = false;
    if (device_descriptor.idVendor == APPLE_VENDOR_ID && (
        device_descriptor.idProduct == IRECOVERY_K_RECOVERY_MODE_1 ||
        device_descriptor.idProduct == IRECOVERY_K_RECOVERY_MODE_2 ||
        device_descriptor.idProduct == IRECOVERY_K_RECOVERY_MODE_3 || 
        device_descriptor.idProduct == IRECOVERY_K_RECOVERY_MODE_4 || 
        device_descriptor.idProduct == IRECOVERY_K_WTF_MODE ||
        device_descriptor.idProduct == IRECOVERY_K_DFU_MODE
    )) is_supported = true;

    if (ret_device_descriptor) *ret_device_descriptor = device_descriptor;

    return is_supported;
}

bool irecovery_client_is_usable(irecovery_client_t client, bool run_event_handler) {
    if (run_event_handler) usb_HandleEvents();
    return client && client->handle && ((usb_GetRole() & USB_ROLE_DEVICE) != USB_ROLE_DEVICE) && device_zone_nonzero(client);
}

static int irecovery_get_string_descriptor_ascii(irecovery_client_t client, uint8_t desc_index, unsigned char* buffer, size_t size) {
    if (!irecovery_client_is_usable(client, true)) {
        return IRECOVERY_E_NO_DEVICE;
    } else if (!buffer) {
        return IRECOVERY_E_BAD_PTR;
    } else if (size == 0) {
        return IRECOVERY_E_DST_BUF_SIZE_ZERO;
    }

    irecovery_log(client, "Getting string descriptor (ascii) at index %" PRIu8 "...\n", desc_index);
    size_t string_descriptor_len = 2 + (size * 2);
    usb_string_descriptor_t* string_descriptor = (usb_string_descriptor_t*)malloc(string_descriptor_len);
    if (!string_descriptor) return IRECOVERY_E_NO_MEMORY;

    size_t transferred = 0;
    if (usb_GetStringDescriptor(client->handle, desc_index, 0, string_descriptor, string_descriptor_len, &transferred) != USB_SUCCESS || transferred == 0) {
        free(string_descriptor);
        return IRECOVERY_E_DESCRIPTOR_FETCH_FAILED;
    }

    size_t string_desc_str_len = (string_descriptor->bLength - 2) / 2;
    size_t i;
    memset(buffer, 0, size);
    for (i = 0; i < string_desc_str_len && i < size - 1; i++) {
        wchar_t wc = string_descriptor->bString[i];
        buffer[i] = (wc <= 0x7F) ? (unsigned char)wc : '?';
    }
    buffer[i] = '\0';

    free(string_descriptor);

    return i;
}

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/src/libirecovery.c#L765 */
static void irecovery_load_device_info_from_iboot_string(irecovery_client_t client, const char* iboot_string) {
    if (!client || !iboot_string) return;

    memset(&client->device_info, 0, sizeof(struct irecovery_device_info));

    client->device_info.serial_string = strdup(iboot_string);

    char* ptr;

    ptr = strstr(iboot_string, "CPID:");
    if (ptr != NULL) {
        nsscanf(ptr, "CPID:%x", &client->device_info.cpid);
    }

    ptr = strstr(iboot_string, "CPRV:");
    if (ptr != NULL) {
        nsscanf(ptr, "CPRV:%x", &client->device_info.cprv);
    }

    ptr = strstr(iboot_string, "CPFM:");
    if (ptr != NULL) {
        nsscanf(ptr, "CPFM:%x", &client->device_info.cpfm);
    }

    ptr = strstr(iboot_string, "SCEP:");
    if (ptr != NULL) {
        nsscanf(ptr, "SCEP:%x", &client->device_info.scep);
    }

    ptr = strstr(iboot_string, "BDID:");
    if (ptr != NULL) {
        uint64_t bdid = 0;
        nsscanf(ptr, "BDID:%" SCNx64, &bdid);
        client->device_info.bdid = (unsigned int)bdid;
    }

    ptr = strstr(iboot_string, "ECID:");
    if (ptr != NULL) {
        nsscanf(ptr, "ECID:%" SCNx64, &client->device_info.ecid);
    }

    ptr = strstr(iboot_string, "IBFL:");
    if (ptr != NULL) {
        nsscanf(ptr, "IBFL:%x", &client->device_info.ibfl);
    }

    char tmp[256];
    tmp[0] = '\0';
    ptr = strstr(iboot_string, "SRNM:[");
    if (ptr != NULL) {
        nsscanf(ptr, "SRNM:[%s]", tmp);
        ptr = strrchr(tmp, ']');
        if (ptr != NULL) {
            *ptr = '\0';
        }
        client->device_info.srnm = strdup(tmp);
    }

    tmp[0] = '\0';
	ptr = strstr(iboot_string, "IMEI:[");
	if (ptr != NULL) {
		nsscanf(ptr, "IMEI:[%s]", tmp);
		ptr = strrchr(tmp, ']');
		if (ptr != NULL) {
			*ptr = '\0';
		}
		client->device_info.imei = strdup(tmp);
	}

    tmp[0] = '\0';
	ptr = strstr(iboot_string, "SRTG:[");
	if (ptr != NULL) {
		nsscanf(ptr, "SRTG:[%s]", tmp);
		ptr = strrchr(tmp, ']');
		if (ptr != NULL) {
			*ptr = '\0';
		}
		client->device_info.srtg = strdup(tmp);
	}

	tmp[0] = '\0';
	ptr = strstr(iboot_string, "PWND:[");
	if (ptr != NULL) {
		nsscanf(ptr, "PWND:[%s]", tmp);
		ptr = strrchr(tmp, ']');
		if (ptr != NULL) {
			*ptr = '\0';
		}
		client->device_info.pwnd = strdup(tmp);
	}

    client->device_info.pid = client->device_descriptor.idProduct;
    client->mode            = client->device_descriptor.idProduct;
}

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/src/libirecovery.c#L1373 */
// Returns transferred bytes on success, negative values are irecovery_error_t error codes
int irecovery_usb_control_transfer(irecovery_client_t client, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, unsigned char* data, uint16_t w_length) {
    if (!irecovery_client_is_usable(client, true)) return IRECOVERY_E_NO_DEVICE;

    usb_control_setup_t setup = {
        .bmRequestType = bm_request_type,
        .bRequest      = b_request,
        .wValue        = w_value,
        .wIndex        = w_index,
        .wLength       = w_length
    };

    size_t transferred = 0;
    if (usb_ControlTransfer(usb_GetDeviceEndpoint(client->handle, 0), &setup, data, 0, &transferred) != USB_SUCCESS) return IRECOVERY_E_USB_UPLOAD_FAILED;

    return transferred;
}

static irecovery_error_t irecovery_get_total_configuration_descriptor(irecovery_client_t client, uint8_t index, usb_configuration_descriptor_t** configuration_descriptor, size_t* length) {
	if (!configuration_descriptor || *configuration_descriptor || !length) return IRECOVERY_E_BAD_PTR;

	*length = usb_GetConfigurationDescriptorTotalLength(client->handle, index);
	if (*length == 0) return IRECOVERY_E_DESCRIPTOR_FETCH_FAILED;
	*configuration_descriptor = (usb_configuration_descriptor_t*)malloc(*length);
	if (!(*configuration_descriptor)) {
		return IRECOVERY_E_NO_MEMORY;
	}

	size_t transferred = 0;
	if (usb_GetConfigurationDescriptor(client->handle, index, *configuration_descriptor, *length, &transferred) != USB_SUCCESS || transferred == 0) {
		free(*configuration_descriptor);
		*length = 0;
		return IRECOVERY_E_DESCRIPTOR_FETCH_FAILED;
	}

	return IRECOVERY_E_SUCCESS;
}

static irecovery_error_t irecovery_usb_set_configuration(irecovery_client_t client, uint8_t configuration) {
    if (!irecovery_client_is_usable(client, true)) return IRECOVERY_E_NO_DEVICE;

    irecovery_log(client, "Setting configuration to %" PRIu8 "...\n", configuration);
	usb_configuration_descriptor_t* configuration_descriptor = NULL;
	size_t length = 0;
    irecovery_error_t irecovery_error = irecovery_get_total_configuration_descriptor(client, configuration, &configuration_descriptor, &length);
	if (irecovery_error != IRECOVERY_E_SUCCESS) return irecovery_error;
	irecovery_log(client, "Configuration %" PRIu8 " is %zu bytes.\n", configuration, length);

    usb_error_t error = usb_SetConfiguration(client->handle, configuration_descriptor, length);
    free(configuration_descriptor);
    if (error == USB_SUCCESS) {
        return IRECOVERY_E_SUCCESS;
    } else {
        return IRECOVERY_E_DESCRIPTOR_SET_FAILED;
    }
}

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/src/libirecovery.c#L854 */
static void irecovery_copy_nonce_with_tag_from_buffer(irecovery_client_t client, const char* tag, unsigned char** nonce, unsigned int* nonce_size, const char *buf)
{
	int taglen = strlen(tag);
	int nlen = 0;
	const char* nonce_string = NULL;
	const char* p = buf;
	char* colon = NULL;
	do {
		colon = strchr(p, ':');
		if (!colon)
			break;
		if (colon-taglen < p) {
			break;
		}
		char *space = strchr(colon, ' ');
		if (strncmp(colon-taglen, tag, taglen) == 0) {
			p = colon+1;
			if (!space) {
				nlen = strlen(p);
			} else {
				nlen = space-p;
			}
			nonce_string = p;
			nlen/=2;
			break;
		} else {
			if (!space) {
				break;
			} else {
				p = space+1;
			}
		}
	} while (colon);

	if (nlen == 0) {
		irecovery_log(client, "%s: WARNING: couldn't find tag %s in string %s\n", __func__, tag, buf);
		return;
	}

	unsigned char *nn = malloc(nlen);
	if (!nn) {
		return;
	}

	int i = 0;
	for (i = 0; i < nlen; i++) {
		int val = 0;
		if (nsscanf(nonce_string+(i*2), "%2x", &val) == 1) {
			nn[i] = (unsigned char)val;
		} else {
			irecovery_log(client, "%s: ERROR: unexpected data in nonce result (%2s)\n", __func__, nonce_string+(i*2));
			break;
		}
	}

	if (i != nlen) {
		irecovery_log(client, "%s: ERROR: unable to parse nonce\n", __func__);
		free(nn);
		return;
	}

	*nonce = nn;
	*nonce_size = nlen;
}

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/src/libirecovery.c#L919 */
static void irecovery_copy_nonce_with_tag(irecovery_client_t client, const char* tag, unsigned char** nonce, unsigned int* nonce_size) {
    if (!irecovery_client_is_usable(client, false) || !tag || !nonce || *nonce || !nonce_size) return;

    char buf[256];
    int len = 0;
    
    memset(buf, 0, sizeof(buf));
    len = irecovery_get_string_descriptor_ascii(client, 1, (unsigned char*)buf, sizeof(buf) - 1);
    if (len < 0) {
        irecovery_log(client, "%s: got length: %d\n", __func__, len);
        return;
    }

    buf[len] = '\0';

    irecovery_copy_nonce_with_tag_from_buffer(client, tag, nonce, nonce_size, buf);
}

// Client must be released manually if this function fails.
static irecovery_error_t irecovery_finalize_client(irecovery_client_t client) {
    if (!irecovery_client_is_usable(client, false)) return IRECOVERY_E_NO_DEVICE;
    if (client->finalized > 0) {
		return IRECOVERY_E_SUCCESS;
	} else if (client->finalized < 0) {
		return IRECOVERY_E_FINALIZATION_BLOCKED;
	}

    // Get the serial string via iSerialNumber
    char serial_str[256];
    memset(serial_str, 0, sizeof(serial_str));
    int ret = irecovery_get_string_descriptor_ascii(client, client->device_descriptor.iSerialNumber, (unsigned char*)serial_str, sizeof(serial_str)-1);
    if (ret < 0) {
        return ret;
    } else {
        // Parse for info
        irecovery_load_device_info_from_iboot_string(client, serial_str);
    }

    // Check ECID
    if (client->ecid_restriction != 0) {
        if (client->ecid_restriction != client->device_info.ecid) {
			// Do not allow finalization again
			irecovery_log(client, "ECID mismatch, finalization will no longer be available.\n");
			client->finalized = -1;
            return IRECOVERY_E_ECID_MISMATCH;
        }
    }

    // Continue configuring this device
    irecovery_error_t error = irecovery_usb_set_configuration(client, 1);
    if (error != IRECOVERY_E_SUCCESS) {
		client->finalized = -1;
		return error;
	}

    irecovery_copy_nonce_with_tag(client, "NONC", &client->device_info.ap_nonce, &client->device_info.ap_nonce_size);
    irecovery_copy_nonce_with_tag(client, "SNON", &client->device_info.sep_nonce, &client->device_info.sep_nonce_size);

    client->finalized = 1;

    irecovery_log(client, "Client @ %p was finalized.\n", (void*)client);
    return error;
}

static usb_error_t usb_event_handler(usb_event_t event, void* event_data, void* callback_data) {
    usb_error_t error = USB_SUCCESS;
    irecovery_client_t client = (irecovery_client_t)callback_data;
    if (!client) return USB_SUCCESS; // Just in case

    switch (event) {
        case USB_ROLE_CHANGED_EVENT: {
            usb_role_t* new_role = event_data;
            if ((*new_role & USB_ROLE_DEVICE) == USB_ROLE_DEVICE) {
                irecovery_log(client, "Calculator is no longer the host.\n");
                irecovery_client_clear_device_zone(client);
            }
            break;
        }

        case USB_DEVICE_DISCONNECTED_EVENT: {
            usb_device_t disconnected_device = event_data;
            irecovery_log(client, "Device @ %p was disconnected.\n", (void*)disconnected_device);
            if (disconnected_device == client->handle) {
                irecovery_client_clear_device_zone(client);
            }
            break;
        }

        case USB_DEVICE_CONNECTED_EVENT: {
            usb_device_t connected_device = event_data;
            irecovery_log(client, "New device @ %p connected.\n", (void*)connected_device);
            irecovery_log(client, "Calculator is ");
            if ((usb_GetRole() & USB_ROLE_DEVICE) == USB_ROLE_DEVICE) {
                irecovery_log(client, "not the host. Ignoring...\n");
                break;
            } else {
                irecovery_log(client, "the host. Resetting...");
                error = usb_ResetDevice(connected_device);
                irecovery_log(client, "%s.\n", (error == USB_SUCCESS) ? "Success" : "Failed");
            }
            break;
        }

        case USB_DEVICE_DISABLED_EVENT: {
            usb_device_t disabled_device = event_data;
            if (disabled_device == client->handle) {
                irecovery_log(client, "Existing ");
            } else {
				irecovery_log(client, "Unrelated ");
			}
			irecovery_log(client, "device @ %p was disabled.\n", (void*)disabled_device);
            break;
        }

        case USB_DEVICE_ENABLED_EVENT: {
            usb_device_t enabled_device = event_data;
            if ((usb_GetRole() & USB_ROLE_DEVICE) == USB_ROLE_DEVICE) {
                irecovery_log(client, "Device @ %p was enabled, but the calculator is not the host. Ignoring...\n", (void*)enabled_device);
                break;
            }
            if (enabled_device == client->handle) {
                irecovery_log(client, "Device @ %p was re-enabled.\n", (void*)enabled_device);
            } else {
                irecovery_log(client, "Determining availability for new connections...\n");
                // Decide whether or not to accept this connection
                irecovery_log(client, "Policy: ");
                if (client->connection_policy == IRECOVERY_CLIENT_DEVICE_POLICY_ACCEPT_ALL) {
                    irecovery_log(client, "accept all.\n");
                    irecovery_client_clear_device_zone(client);
                } else if (client->connection_policy == IRECOVERY_CLIENT_DEVICE_POLICY_ACCEPT_ONLY_WHEN_NO_CURRENT_CONNECTION) {
                    irecovery_log(client, "accept when not connected (currently ");
                    if (irecovery_client_is_usable(client, false)) {
                        irecovery_log(client, "connected).\n");
                        break;
                    } else {
                        irecovery_log(client, "not connected).\n");
                    }
                } else if (client->connection_policy == IRECOVERY_CLIENT_DEVICE_POLICY_ONE_CONNECTION_LIMIT) {
                    irecovery_log(client, "one connection limit (new connection allowed: ");
                    if (client->num_connections == 1) {
                        irecovery_log(client, "no.)\n");
                        break;
                    } else {
                        irecovery_log(client, "yes.)\n");
                    }
                }

                if (irecovery_device_is_supported(enabled_device, &client->device_descriptor)) {
                    irecovery_log(client, "Device @ %p is ready to be handled.\n", (void*)enabled_device);
                    client->handle = enabled_device;
                } else {
                    irecovery_log(client, "Device @ %p is not handleable. Ignoring...\n", (void*)enabled_device);
                    irecovery_client_clear_device_zone(client);
                }
            }
            break;
        }

        default:
            break;
    }

    return error;
}

const char* irecovery_strerror(irecovery_error_t error) {
    switch (error) {
        case IRECOVERY_E_SUCCESS:
            return "Success.";
        case IRECOVERY_E_BAD_PTR:
            return "An invalid pointer was passed to a function.";
        case IRECOVERY_E_CLIENT_ALREADY_ACTIVE:
            return "The provided client is already active.";
        case IRECOVERY_E_NO_MEMORY:
            return "Out of memory.";
        case IRECOVERY_E_USB_INIT_FAILED:
            return "Failed to initialize the USB backend.";
        case IRECOVERY_E_NO_DEVICE:
            return "No device.";
        case IRECOVERY_E_DST_BUF_SIZE_ZERO:
            return "A destination buffer's size is zero.";
        case IRECOVERY_E_DESCRIPTOR_FETCH_FAILED:
            return "Failed to fetch a descriptor from the device.";
        case IRECOVERY_E_ECID_MISMATCH:
            return "The queried device does not match the ECID restriction of the client.";
        case IRECOVERY_E_DESCRIPTOR_SET_FAILED:
            return "Failed to set a descriptor/property of the device.";
		case IRECOVERY_E_INTERFACE_SET_FAILED:
			return "Failed to set the interface of the device.";
		case IRECOVERY_E_FINALIZATION_BLOCKED:
			return "Finalization is not allowed right now.";
		case IRECOVERY_E_USB_UPLOAD_FAILED:
			return "Failed to upload data to the device.";
		case IRECOVERY_E_INVALID_USB_STATUS:
			return "The device is in an invalid state.";
		case IRECOVERY_E_COMMAND_TOO_LONG:
			return "The provided command was too long.";
		case IRECOVERY_E_NO_COMMAND:
			return "There was no command to handle.";
		case IRECOVERY_E_SERVICE_NOT_AVAILABLE:
			return "The device's mode doesn't support this function.";
		case IRECOVERY_E_USB_RESET_FAILED:
			return "Failed to reset the USB device.";
		case IRECOVERY_E_UNKNOWN_EVENT_TYPE:
			return "The provided event type is unknown.";
        default:
            return "Foreign error.";
    }
}

irecovery_error_t irecovery_client_new(irecovery_connection_policy_t connection_policy, uint64_t ecid, irecovery_log_cb_t logger, irecovery_client_t* client) {
    if (!client) {
        return IRECOVERY_E_BAD_PTR;
    } else if (*client) {
        return IRECOVERY_E_CLIENT_ALREADY_ACTIVE;
    }

    // Allocate and set user members
    *client = (irecovery_client_t)calloc(1, sizeof(struct irecovery_client));
    if (!(*client)) {
        return IRECOVERY_E_NO_MEMORY;
    } else {
        // Set Static Zone
        (*client)->connection_policy = connection_policy;
        (*client)->log_fp            = logger;
        (*client)->ecid_restriction  = ecid;
        irecovery_log(*client, "Logs are enabled.\n");
    }

    irecovery_log(*client, "Initializing USB...\n");
    if (usb_Init(usb_event_handler, *client, NULL, USB_DEFAULT_INIT_FLAGS) != USB_SUCCESS) {
        irecovery_log(*client, "Failed.\n");
        usb_Cleanup();
        irecovery_client_free(client);
        return IRECOVERY_E_USB_INIT_FAILED;
    } else { irecovery_log(*client, "Success.\n"); }

    return IRECOVERY_E_SUCCESS;
}

irecovery_error_t irecovery_poll_for_device(irecovery_client_t client) {
    if (!client) return IRECOVERY_E_BAD_PTR;

    usb_HandleEvents();
    return irecovery_finalize_client(client);
}

irecovery_error_t irecovery_reset(irecovery_client_t client) {
	if (!irecovery_client_is_usable(client, true)) return IRECOVERY_E_NO_DEVICE;

	if (usb_ResetDevice(client->handle) == USB_SUCCESS) {
		return IRECOVERY_E_SUCCESS;
	} else {
		return IRECOVERY_E_USB_RESET_FAILED;
	}
}

void irecovery_client_free(irecovery_client_t* client) {
    if (!client || !(*client)) return;

    irecovery_log(*client, "Freeing client @ %p...\n", (void*)*client);

    usb_Cleanup();
    irecovery_client_clear_device_zone(*client);
    free(*client);
    *client = NULL;
}

/* https://github.com/libimobiledevice/libirecovery/blob/638056a593b3254d05f2960fab836bace10ff105/src/libirecovery.c#L1501 */
int irecovery_usb_bulk_transfer(irecovery_client_t client, unsigned char endpoint, unsigned char* data, size_t length, size_t* transferred) {	
	if (!irecovery_client_is_usable(client, true)) return IRECOVERY_E_NO_DEVICE;
    
    size_t _transferred = 0;
    usb_error_t error = usb_Transfer(usb_GetDeviceEndpoint(client->handle, endpoint), data, length, 0, &_transferred);
    if (error != USB_SUCCESS) {
        return IRECOVERY_E_USB_UPLOAD_FAILED;
    } else {
        *transferred = _transferred;
        return _transferred;
    }
}

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/src/libirecovery.c#L3857 */
irecovery_error_t irecovery_reset_counters(irecovery_client_t client) {
	if (!irecovery_client_is_usable(client, true)) return IRECOVERY_E_NO_DEVICE;

	if (client->mode == IRECOVERY_K_DFU_MODE || client->mode == IRECOVERY_K_WTF_MODE) {
		int ret;
		if ((ret = irecovery_usb_control_transfer(client, 0x21, 4, 0, 0, NULL, 0)) < 0) return ret;
	}

	return IRECOVERY_E_SUCCESS;
}

/* https://github.com/libimobiledevice/libirecovery/blob/638056a593b3254d05f2960fab836bace10ff105/src/libirecovery.c#L3214 */
static irecovery_error_t irecovery_get_status(irecovery_client_t client, unsigned int* status) {
	if (!irecovery_client_is_usable(client, true)) {
		*status = 0;
		return IRECOVERY_E_NO_DEVICE;
	}

	unsigned char buffer[6];
	memset(buffer, 0, 6);
	if (irecovery_usb_control_transfer(client, 0xA1, 3, 0, 0, buffer, 6) != 6) {
		*status = 0;
		return IRECOVERY_E_INVALID_USB_STATUS;
	}

	*status = (unsigned int)buffer[4];

	return IRECOVERY_E_SUCCESS;
}

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/src/libirecovery.c#L3920 */
irecovery_error_t irecovery_finish_transfer(irecovery_client_t client) {
	if (!irecovery_client_is_usable(client, true)) return IRECOVERY_E_NO_DEVICE;

	irecovery_usb_control_transfer(client, 0x21, 1, 0, 0, NULL, 0);

	unsigned int status = 0;
	for (int i = 0; i < 3; i++) {
		irecovery_get_status(client, &status);
	}

	irecovery_reset(client);

	return IRECOVERY_E_SUCCESS;
}

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/src/libirecovery.c#L3602 */
irecovery_error_t irecovery_get_mode(irecovery_client_t client, int* mode) {
    if (!mode) {
        return IRECOVERY_E_BAD_PTR;
    } else if (!irecovery_client_is_usable(client, true)) {
        return IRECOVERY_E_NO_DEVICE;
    }

	if (client->device_info.pwnd != NULL) {
		*mode = IRECOVERY_K_PWNDFU_MODE;
	} else {
		*mode = client->mode;
	}
	
    return IRECOVERY_E_SUCCESS;
}

const char* irecovery_mode_to_str(int mode) {
    switch (mode) {
        case IRECOVERY_K_RECOVERY_MODE_1:
        case IRECOVERY_K_RECOVERY_MODE_2:
        case IRECOVERY_K_RECOVERY_MODE_3:
        case IRECOVERY_K_RECOVERY_MODE_4:
            return "Recovery";
        case IRECOVERY_K_WTF_MODE:
            return "WTF";
        case IRECOVERY_K_DFU_MODE:
            return "DFU";
		case IRECOVERY_K_PWNDFU_MODE:
			return "PWNDFU";
        default:
            return "Unknown";
    }
}

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/src/libirecovery.c#L2104 */
irecovery_error_t irecovery_event_subscribe(irecovery_client_t client, irecovery_event_type type, irecovery_event_cb_t callback) {
	if (!irecovery_client_is_usable(client, false)) {
		return IRECOVERY_E_NO_DEVICE;
	} else if (!callback) {
		return IRECOVERY_E_BAD_PTR;
	}
	
	switch (type) {
		case IRECOVERY_PROGRESS:
			client->progress_callback = callback;
			break;

		default:
			return IRECOVERY_E_UNKNOWN_EVENT_TYPE;
	}

	return IRECOVERY_E_SUCCESS;
}

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/src/libirecovery.c#L2142 */
irecovery_error_t irecovery_event_unsubscribe(irecovery_client_t client, irecovery_event_type type) {
	if (!irecovery_client_is_usable(client, false)) {
		return IRECOVERY_E_NO_DEVICE;
	}

	switch (type) {
		case IRECOVERY_PROGRESS:
			client->progress_callback = NULL;
			break;

		default:
			return IRECOVERY_E_UNKNOWN_EVENT_TYPE;
	}

	return IRECOVERY_E_SUCCESS;
}

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/src/libirecovery.c#L3108 */
static irecovery_error_t irecovery_send_command_raw(irecovery_client_t client, const char* command, uint8_t b_request) {
	if (!irecovery_client_is_usable(client, true)) {
		return IRECOVERY_E_NO_DEVICE;
	} else if (!command) {
		return IRECOVERY_E_BAD_PTR;
	} else if (client->mode != IRECOVERY_K_RECOVERY_MODE_1 && client->mode != IRECOVERY_K_RECOVERY_MODE_2 && client->mode != IRECOVERY_K_RECOVERY_MODE_3 && client->mode != IRECOVERY_K_RECOVERY_MODE_4) {
		return IRECOVERY_E_SERVICE_NOT_AVAILABLE;
	}
	
	size_t length = strlen(command);
	if (length >= 256) return IRECOVERY_E_COMMAND_TOO_LONG;

	if (length > 0) {
		int ret = irecovery_usb_control_transfer(client, 0x40, b_request, 0, 0, (unsigned char*)command, length + 1);
		if (ret < 0) return ret;
	} else {
		return IRECOVERY_E_NO_COMMAND;
	}

	return IRECOVERY_E_SUCCESS;
}

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/src/libirecovery.c#L3123 */
irecovery_error_t irecovery_send_command_breq(irecovery_client_t client, const char* command, uint8_t b_request) {
	if (!irecovery_client_is_usable(client, true)) {
		return IRECOVERY_E_NO_DEVICE;
	} else if (!command) {
		return IRECOVERY_E_BAD_PTR;
	} else if (client->mode != IRECOVERY_K_RECOVERY_MODE_1 && client->mode != IRECOVERY_K_RECOVERY_MODE_2 && client->mode != IRECOVERY_K_RECOVERY_MODE_3 && client->mode != IRECOVERY_K_RECOVERY_MODE_4) {
		return IRECOVERY_E_SERVICE_NOT_AVAILABLE;
	}

	size_t length = strlen(command);
	if (length >= 256) return IRECOVERY_E_COMMAND_TOO_LONG;

	irecovery_error_t error = irecovery_send_command_raw(client, command, b_request);
	if (error != IRECOVERY_E_SUCCESS) {
		irecovery_log(client, "Failed to send command %s\n", command);
	}

	return error;
}

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/tools/irecovery.c#L222 */
static bool irecovery_is_breq_command(const char* cmd) {
	return (
		!strcmp(cmd, "go")
		|| !strcmp(cmd, "bootx")
		|| !strcmp(cmd, "reboot")
		|| !strcmp(cmd, "memboot")
	);
}

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/src/libirecovery.c#L3168 */
irecovery_error_t irecovery_send_command(irecovery_client_t client, const char* command) {
	// Client eligibility checked by irecovery_send_command_breq()
	if (!command) return IRECOVERY_E_BAD_PTR;

	return irecovery_send_command_breq(client, command, irecovery_is_breq_command(command));
}

/* https://github.com/libimobiledevice/libirecovery/blob/638056a593b3254d05f2960fab836bace10ff105/src/libirecovery.c#L3206 */
irecovery_error_t irecovery_send_buffer(irecovery_client_t client, unsigned char* buffer, size_t length, unsigned int options) {
	if (!irecovery_client_is_usable(client, true)) {
        return IRECOVERY_E_NO_DEVICE;
    } else if (!buffer) {
        return IRECOVERY_E_BAD_PTR;
    }

	irecovery_error_t error = IRECOVERY_E_SUCCESS;
	bool recovery_mode = (client->mode != IRECOVERY_K_DFU_MODE && client->mode != IRECOVERY_K_WTF_MODE);

	uint32_t h1 = 0xFFFFFFFF;
	unsigned char dfu_xbuf[12] = {0xff, 0xff, 0xff, 0xff, 0xac, 0x05, 0x00, 0x01, 0x55, 0x46, 0x44, 0x10};
	int dfu_crc = 1;
	size_t packet_size = recovery_mode ? 0x8000 : 0x800;
	int last = length % packet_size;
	int packets = length / packet_size;
	
	if (last != 0) {
		packets++;
	} else {
		last = packet_size;
	}

	// initiate transfer
	if (recovery_mode) {
		error = irecovery_usb_control_transfer(client, 0x41, 0, 0, 0, NULL, 0);
	} else {
		uint8_t state = 0;
		if (irecovery_usb_control_transfer(client, 0xa1, 5, 0, 0, (unsigned char*)&state, 1) == 1) {
			error = IRECOVERY_E_SUCCESS;
		} else {
			return IRECOVERY_E_USB_UPLOAD_FAILED;
		}
		switch (state) {
			case 2:
				// DFU IDLE
				break;
			case 10:
				irecovery_log(client, "DFU ERROR, issuing CLRSTATUS\n");
				irecovery_usb_control_transfer(client, 0x21, 4, 0, 0, NULL, 0);
				error = IRECOVERY_E_USB_UPLOAD_FAILED;
				break;
			default:
				irecovery_log(client, "Unexpected state %d, issuing ABORT\n", state);
				irecovery_usb_control_transfer(client, 0x21, 6, 0, 0, NULL, 0);
				error = IRECOVERY_E_USB_UPLOAD_FAILED;
				break;
		}
	}

	if (error != IRECOVERY_E_SUCCESS) return error;

	size_t count = 0;
	unsigned int status = 0;
	size_t bytes = 0;
	for (int i = 0; i < packets; i++) {
		size_t size = (i + 1) < packets ? packet_size : last;

		// Use bulk transfer for recovery mode and control transfer for DFU and WTF mode
		if (recovery_mode) {
			error = irecovery_usb_bulk_transfer(client, 0x04, &buffer[i * packet_size], size, &bytes);
		} else {
			if (dfu_crc) {
				size_t j;
				for (j = 0; j < size; j++) {
					crc32_step(h1, buffer[i*packet_size + j]);
				}
			}
			if (dfu_crc && i+1 == packets) {
				int j;
				if (size+16 > packet_size) {
					bytes = irecovery_usb_control_transfer(client, 0x21, 1, i, 0, &buffer[i * packet_size], size);
					if (bytes != size) return IRECOVERY_E_USB_UPLOAD_FAILED;
					count += size;
					size = 0;
				}
				for (j = 0; j < 2; j++) {
					crc32_step(h1, dfu_xbuf[j*6 + 0]);
					crc32_step(h1, dfu_xbuf[j*6 + 1]);
					crc32_step(h1, dfu_xbuf[j*6 + 2]);
					crc32_step(h1, dfu_xbuf[j*6 + 3]);
					crc32_step(h1, dfu_xbuf[j*6 + 4]);
					crc32_step(h1, dfu_xbuf[j*6 + 5]);
				}

				char* newbuf = (char*)malloc(size + 16);
				if (!newbuf) return IRECOVERY_E_NO_MEMORY;
				if (size > 0) memcpy(newbuf, &buffer[i * packet_size], size);
				memcpy(newbuf+size, dfu_xbuf, 12);
				newbuf[size+12] = h1 & 0xFF;
				newbuf[size+13] = (h1 >> 8) & 0xFF;
				newbuf[size+14] = (h1 >> 16) & 0xFF;
				newbuf[size+15] = (h1 >> 24) & 0xFF;
				size += 16;
				bytes = irecovery_usb_control_transfer(client, 0x21, 1, i, 0, (unsigned char*)newbuf, size);
				free(newbuf);
			} else {
				bytes = irecovery_usb_control_transfer(client, 0x21, 1, i, 0, &buffer[i * packet_size], size);
			}
		}

		if (bytes != size) return IRECOVERY_E_USB_UPLOAD_FAILED;

		if (!recovery_mode) error = irecovery_get_status(client, &status);

		if (error != IRECOVERY_E_SUCCESS) return error;

		if (!recovery_mode && status != 5) {
			int retry = 0;

			while (retry++ < 20) {
				irecovery_get_status(client, &status);
				if (status == 5) break;
				sleep(1);
			}

			if (status != 5) return IRECOVERY_E_USB_UPLOAD_FAILED;
		}

		count += size;
		if (client->progress_callback) {
			irecovery_event_t event = {
				.size     = count,
				.data     = (char*)"Uploading",
				.progress = ((double)count / (double)length) * 100.0,
				.type     = IRECOVERY_PROGRESS
			};
			client->progress_callback(client, &event);
		} else {
			irecovery_log(client, "Sent %d bytes - %lu of %zu\n", bytes, count, length);
		}
	}

	if (recovery_mode && length % 512 == 0) {
		// send a ZLP
		bytes = 0;
		irecovery_usb_bulk_transfer(client, 0x04, buffer, 0, &bytes);
	}

	if ((options & IRECOVERY_SEND_OPT_DFU_NOTIFY_FINISH) && !recovery_mode) {
		irecovery_usb_control_transfer(client, 0x21, 1, packets, 0, (unsigned char*)buffer, 0);

		for (int i = 0; i < 2; i++) {
			error = irecovery_get_status(client, &status);
			if (error != IRECOVERY_E_SUCCESS) return error;
		}

		if ((options & IRECOVERY_SEND_OPT_DFU_FORCE_ZLP)) {
			// we send a pseudo ZLP here just in case
			irecovery_usb_control_transfer(client, 0x21, 0, 0, 0, NULL, 0);
		}

		usb_ResetDevice(client->handle);
	}
	
	return IRECOVERY_E_SUCCESS;
}

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/src/libirecovery.c#L3730 */
irecovery_error_t irecovery_saveenv(irecovery_client_t client) {
	// Client checked by irecovery_send_command_raw().
	return irecovery_send_command_raw(client, "saveenv", 0);
}

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/src/libirecovery.c#L3535 */
irecovery_error_t irecovery_getenv(irecovery_client_t client, const char* variable, char** value) {
	// Client checked by irecovery_send_command_raw().
	if (!variable || !value || *value) return IRECOVERY_E_BAD_PTR;

	char command[256];
	memset(command, 0, sizeof(command));
	snprintf(command, sizeof(command)-1, "getenv %s", variable);
	irecovery_error_t error = irecovery_send_command_raw(client, command, 0);
	if (error != IRECOVERY_E_SUCCESS) return error;

	size_t response_size = 256;
	char* response = (char*)calloc(1, response_size);
	if (!response) return IRECOVERY_E_NO_MEMORY;

	int ret = irecovery_usb_control_transfer(client, 0xC0, 0, 0, 0, (unsigned char*)response, response_size-1);
	if (ret < 0) {
		free(response);
		return ret;
	} else {
		*value = response;
	}

	return IRECOVERY_E_SUCCESS;
}

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/src/libirecovery.c#L3744 */
irecovery_error_t irecovery_setenv(irecovery_client_t client, const char* variable, const char* value) {
	// Client checked by irecovery_send_command_raw().
	if (!variable || !value) return IRECOVERY_E_BAD_PTR;

	char command[256];
	memset(command, 0, sizeof(command));
	snprintf(command, sizeof(command)-1, "setenv %s %s", variable, value);
	return irecovery_send_command_raw(client, command, 0);
}

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/src/libirecovery.c#L3769 */
irecovery_error_t irecovery_setenv_np(irecovery_client_t client, const char* variable, const char* value) {
	// Client checked by irecovery_send_command_raw().
	if (!variable || !value) return IRECOVERY_E_BAD_PTR;

	char command[256];
	memset(command, 0, sizeof(command));
	snprintf(command, sizeof(command)-1, "setenvnp %s %s", variable, value);
	return irecovery_send_command_raw(client, command, 0);
}

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/src/libirecovery.c#L3794 */
irecovery_error_t irecovery_reboot(irecovery_client_t client) {
	// Client checked by irecovery_send_command_raw().
	return irecovery_send_command_raw(client, "reboot", 0);
}

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/src/libirecovery.c#L3577 */
irecovery_error_t irecovery_getret(irecovery_client_t client, unsigned int* value) {
	if (!irecovery_client_is_usable(client, true)) {
		return IRECOVERY_E_NO_DEVICE;
	} else if (!value) {
		return IRECOVERY_E_BAD_PTR;
	}

	*value = 0;

	size_t response_size = 256;
	char* response = (char*)calloc(1, response_size);
	if (!response) return IRECOVERY_E_NO_MEMORY;

	int ret = irecovery_usb_control_transfer(client, 0xC0, 0, 0, 0, (unsigned char*)response, response_size-1);
	if (ret < 0) {
		free(response);
		return ret;
	} else {
		*value = (unsigned int)*response;
		free(response);
	}

	return IRECOVERY_E_SUCCESS;
}

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/src/libirecovery.c#L3616 */
const struct irecovery_device_info* irecovery_get_device_info(irecovery_client_t client) {
    if (!irecovery_client_is_usable(client, true)) return NULL;

    return &client->device_info;
}

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/src/libirecovery.c#L3943 */
irecovery_device_t irecovery_devices_get_all(void) {
    return irecovery_devices;
}

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/src/libirecovery.c#L3948 */
irecovery_error_t irecovery_devices_get_device_by_client(irecovery_client_t client, irecovery_device_t* device) {
    if (!client || !device) return IRECOVERY_E_BAD_PTR;

    *device = NULL;

    unsigned int cpid_match = client->device_info.cpid;
    unsigned int bdid_match = client->device_info.bdid;
    
    for (int i = 0; irecovery_devices[i].hardware_model != NULL; i++) {
        if (irecovery_devices[i].chip_id == cpid_match && irecovery_devices[i].board_id == bdid_match) {
            *device = &irecovery_devices[i];
            return IRECOVERY_E_SUCCESS;
        }
    }

    return IRECOVERY_E_NO_DEVICE;
}

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/src/libirecovery.c#L3982 */
irecovery_error_t irecovery_devices_get_device_by_product_type(const char* product_type, irecovery_device_t* device) {
    if (!product_type || !device) return IRECOVERY_E_BAD_PTR;

    *device = NULL;

    for (int i = 0; irecovery_devices[i].product_type != NULL; i++) {
        if (!strcmp(product_type, irecovery_devices[i].product_type)) {
            *device = &irecovery_devices[i];
            return IRECOVERY_E_SUCCESS;
        }
    }

    return IRECOVERY_E_NO_DEVICE;
}

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/src/libirecovery.c#L4001 */
irecovery_error_t irecovery_devices_get_device_by_hardware_model(const char* hardware_model, irecovery_device_t* device) {
    if (!hardware_model || !device) return IRECOVERY_E_BAD_PTR;

    *device = NULL;

    for (int i = 0; irecovery_devices[i].hardware_model != NULL; i++) {
        if (!strcmp(hardware_model, irecovery_devices[i].hardware_model)) {
            *device = &irecovery_devices[i];
            return IRECOVERY_E_SUCCESS;
        }
    }

    return IRECOVERY_E_NO_DEVICE;
}