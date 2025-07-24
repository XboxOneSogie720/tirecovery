/*
 * irecovery.h
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

#ifndef IRECOVERY_H
#define IRECOVERY_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    IRECOVERY_E_SUCCESS                 = 0,
    IRECOVERY_E_BAD_PTR                 = -1,
    IRECOVERY_E_CLIENT_ALREADY_ACTIVE   = -2,
    IRECOVERY_E_NO_MEMORY               = -3,
    IRECOVERY_E_USB_INIT_FAILED         = -4,
    IRECOVERY_E_NO_DEVICE               = -5,
    IRECOVERY_E_DST_BUF_SIZE_ZERO       = -6,
    IRECOVERY_E_DESCRIPTOR_FETCH_FAILED = -7,
    IRECOVERY_E_ECID_MISMATCH           = -8,
    IRECOVERY_E_DESCRIPTOR_SET_FAILED   = -9,
    IRECOVERY_E_INTERFACE_SET_FAILED    = -10,
    IRECOVERY_E_FINALIZATION_BLOCKED    = -11,
    IRECOVERY_E_USB_UPLOAD_FAILED       = -12,
    IRECOVERY_E_INVALID_USB_STATUS      = -13,
    IRECOVERY_E_COMMAND_TOO_LONG        = -14,
    IRECOVERY_E_NO_COMMAND              = -15,
    IRECOVERY_E_SERVICE_NOT_AVAILABLE   = -16,
    IRECOVERY_E_USB_RESET_FAILED        = -17,
    IRECOVERY_E_UNKNOWN_EVENT_TYPE      = -18
} irecovery_error_t;

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/include/libirecovery.h#L40 */
enum irecovery_mode {
	IRECOVERY_K_RECOVERY_MODE_1   = 0x1280,
	IRECOVERY_K_RECOVERY_MODE_2   = 0x1281,
	IRECOVERY_K_RECOVERY_MODE_3   = 0x1282,
	IRECOVERY_K_RECOVERY_MODE_4   = 0x1283,
	IRECOVERY_K_WTF_MODE          = 0x1222,
	IRECOVERY_K_DFU_MODE          = 0x1227,
    IRECOVERY_K_PWNDFU_MODE       = 0xffff
};

/* https://github.com/libimobiledevice/libirecovery/blob/638056a593b3254d05f2960fab836bace10ff105/include/libirecovery.h#L125 */
enum {
	IRECOVERY_SEND_OPT_NONE              = 0,
	IRECOVERY_SEND_OPT_DFU_NOTIFY_FINISH = (1 << 0),
	IRECOVERY_SEND_OPT_DFU_FORCE_ZLP     = (1 << 1),
	IRECOVERY_SEND_OPT_DFU_SMALL_PKT     = (1 << 2)
};

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/include/libirecovery.h#L83 */
struct irecovery_device {
    const char* product_type;
    const char* hardware_model;
    unsigned int board_id;
    unsigned int chip_id;
    const char* display_name;
};
typedef struct irecovery_device* irecovery_device_t;

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/include/libirecovery.h#L92 */
struct irecovery_device_info {
    unsigned int cpid;
    unsigned int cprv;
    unsigned int cpfm;
    unsigned int scep;
    unsigned int bdid;
    uint64_t ecid;
    unsigned int ibfl;
    char* srnm;
    char* imei;
    char* srtg;
    char* serial_string;
    char* pwnd;
    unsigned char* ap_nonce;
    unsigned int ap_nonce_size;
    unsigned char* sep_nonce;
    unsigned int sep_nonce_size;
    uint16_t pid;
};

typedef enum {
    IRECOVERY_CLIENT_DEVICE_POLICY_ACCEPT_ALL,                             // Allow a new connection to discard an ongoing connection.
                                                                           // Know that if a new connection fails, the previous connection won't be available.
    IRECOVERY_CLIENT_DEVICE_POLICY_ACCEPT_ONLY_WHEN_NO_CURRENT_CONNECTION, // Allow a new connection only if there's no current connection.
    IRECOVERY_CLIENT_DEVICE_POLICY_ONE_CONNECTION_LIMIT                    // Ignore new connections after the initial one.
} irecovery_connection_policy_t;

typedef void (*irecovery_log_cb_t)(const char c);

typedef struct irecovery_client* irecovery_client_t;

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/include/libirecovery.h#L67 */
typedef enum {
    IRECOVERY_PROGRESS = 1
} irecovery_event_type;

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/include/libirecovery.h#L76 */
typedef struct {
    size_t size;
    const char* data;
    double progress;
    irecovery_event_type type;
} irecovery_event_t;

/* https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/include/libirecovery.h#L163C1-L165C95 */
// If your callback function returns something other than 0, the associated irecovery API function will exit early.
typedef int(*irecovery_event_cb_t)(irecovery_client_t client, const irecovery_event_t* event);

/**
 * @brief Logs a message to the screen.
 * @param client The client to reference the log function pointer from.
 * @param fmt The null-terminated string you want to print with formatting supported.
 * @note If you didn't pass a log function pointer into irecovery_client_new(), this function does nothing.
 */
void irecovery_log(irecovery_client_t client, const char* fmt, ...);

/**
 * @brief Returns a human-readable version of the supplied error code.
 * @param[in] error The error code.
 * @return A human-readable representation of said error code.
 */
const char* irecovery_strerror(irecovery_error_t error);

/**
 * @brief Allocates a new client and initializes the USB backend.
 * @param[in] connection_policy The connection policy to use. See irecovery_connection_policy_t.
 * @param[in] ecid ECID restrictions (in decimal) for this client. Set to 0 for no restrictions.
 * @param[in] logger Function pointer to a function like void putc(const char c) used to log events regarding this client.
 *                   Leave NULL to disable logging.
 * @param[out] client Pointer where to store the new client.
 * @return An irecovery_error_t error code.
 * @note If called again, all other clients are invalidated, but you still need to call irecovery_client_free() on them.
 * @attention Don't forget to call irecovery_client_free() before your program exits so that not only you release the allocated
 *            memory but also so the USB backend deinit can be taken care of.
 */
irecovery_error_t irecovery_client_new(irecovery_connection_policy_t connection_policy, uint64_t ecid, irecovery_log_cb_t logger, irecovery_client_t* client);

/**
 * @brief Determines if the given client is able to be communicated with.
 * @param[in] client The client to poll.
 * @param[in] run_event_handler Whether or not to run the event handler.
 *                              If true, the result will take into account if the device was disconnected, for example.
 * @return Whether or not the given client is able to be communicated with.
 */
bool irecovery_client_is_usable(irecovery_client_t client, bool run_event_handler);

/**
 * @brief Removes all device connection attributes from the provided client.
 * @param[in] client The client to clear.
 */
void irecovery_client_clear_device_zone(irecovery_client_t client);

/**
 * @brief Performs a USB control transfer with the device.
 * @param[in] client The client to send the request to.
 * @param[in] bm_request_type USB standard bm_request_type.
 * @param[in] b_request USB standard b_request.
 * @param[in] w_value USB standard w_value.
 * @param[in] w_index USB standard w_index.
 * @param[in] data Data to send (optional).
 * @param[in] w_length Length of the data (if any).
 * @return Transferred bytes on success, negative values are irecovery_error_t error codes.
 */
int irecovery_usb_control_transfer(irecovery_client_t client, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, unsigned char* data, uint16_t w_length);

/**
 * @brief Performs a USB bulk transfer with the device.
 * @param[in] client The client to send the request to.
 * @param[in] endpoint Endpoint address to send/get data to/from.
 * @param[in] data Data to send.
 * @param[in] length Length of the data.
 * @param[out] transferred Bytes transferred.
 * @return Returns transferred bytes on success, negative values are irecovery_error_t error codes.
 */
int irecovery_usb_bulk_transfer(irecovery_client_t client, unsigned char endpoint, unsigned char* data, size_t length, size_t* transferred);

/**
 * @brief Polls for devices in a single run per call.
 * @param[in] client The client to work with.
 * @return IRECOVERY_E_SUCCESS when a device is connected, IRECOVERY_E_NO_DEVICE when there's no device, or another irecovery_error_t error code.
 * @note Call this in a loop with some condition to exit. e.g. while the user isn't pressing a key.
 */
irecovery_error_t irecovery_poll_for_device(irecovery_client_t client);

/**
 * @brief Resets the USB device.
 * @param[in] client The client to reset.
 * @return An irecovery_error_t error code.
 * @see https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/include/libirecovery.h#L141
 */
irecovery_error_t irecovery_reset(irecovery_client_t client);

/**
 * @brief Frees an irecovery client.
 * @param[in] client Pointer to your client.
 * @note Your client at the dst of the provided pointer will be set to NULL for you.
 * @attention You must call this at least once before the program ends if you've called irecovery_client_new()
 *            so the USB backend deinit can be taken care of.
 */
void irecovery_client_free(irecovery_client_t* client);

/**
 * @brief Sends a request to the device to reset on-device counters.
 * @param[in] client The client to send the request to.
 * @return An irecovery_error_t error code.
 * @see https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/include/libirecovery.h#L148
 */
irecovery_error_t irecovery_reset_counters(irecovery_client_t client);

/**
 * @brief Sends an update to the device letting it know a transfer finished.
 * @param[in] client The client to send the request to.
 * @return An irecovery_error_t error code.
 * @see https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/include/libirecovery.h#L149C25-L149C46
 */
irecovery_error_t irecovery_finish_transfer(irecovery_client_t client);

/**
 * @brief Retrieves the given client's mode.
 * @param[in] client The client to query.
 * @param[out] mode The mode to return.
 * @return An irecovery_error_t error code.
 * @see https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/include/libirecovery.h#L183
 */
irecovery_error_t irecovery_get_mode(irecovery_client_t client, int* mode);

/**
 * @brief Gets the string representation of the mode.
 * @param[in] mode The mode the device is in.
 * @return A string representation of the mode.
 */
const char* irecovery_mode_to_str(int mode);

/**
 * @brief Subscribes to an event type.
 * @param[in] client The client to subscribe with.
 * @param[in] type The type of event to subscribe to.
 * @param[in] callback Callback function to run when this event occurs.
 * @return An irecovery_error_t error code.
 * @see https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/include/libirecovery.h#L164
 */
irecovery_error_t irecovery_event_subscribe(irecovery_client_t client, irecovery_event_type type, irecovery_event_cb_t callback);

/**
 * @brief Unsubscribes from an event type.
 * @param[in] client The client to unsubscribe with.
 * @param[in] type The type of event to unsubscribe from.
 * @return An irecovery_error_t error code.
 */
irecovery_error_t irecovery_event_unsubscribe(irecovery_client_t client, irecovery_event_type type);

/**
 * @brief Sends a command to a supported device.
 * @param[in] client The client to send the command to.
 * @param[in] command The null-terminated command string.
 * @return An irecovery_error_t error code.
 * @note b_request is set automatically depending on the command.
 * @see https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/include/libirecovery.h#L169
 */
irecovery_error_t irecovery_send_command(irecovery_client_t client, const char* command);

/**
 * @brief Sends a command to a supported device.
 * @param[in] client The client to send the command to.
 * @param[in] command The null-terminated command string.
 * @param b_request The b_request to set during the control transfer.
 * @return An irecovery_error_t error code.
 * @note b_request is NOT set automatically depending on the command.
 * @see https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/include/libirecovery.h#L170
 */
irecovery_error_t irecovery_send_command_breq(irecovery_client_t client, const char* command, uint8_t b_request);

/**
 * @brief Sends a buffer to the currently connected device (if any).
 * @param[in] client The client to send the buffer to.
 * @param[in] buffer The buffer to send.
 * @param[in] length Length of the buffer.
 * @param[in] options IRECOVERY_SEND_XXX options.
 * @return An irecovery_error_t error code.
 * @see https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/include/libirecovery.h#L171
 */
irecovery_error_t irecovery_send_buffer(irecovery_client_t client, unsigned char* buffer, size_t length, unsigned int options);

/**
 * @brief Tells the device console to save all environment variables.
 * @param[in] client The client to send the request to.
 * @return An irecovery_error_t error code.
 * @see https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/include/libirecovery.h#L175
 */
irecovery_error_t irecovery_saveenv(irecovery_client_t client);

/**
 * @brief Gets an environment variable's value from the device.
 * @param[in] client The client to send the request to.
 * @param[in] variable The name of the environment variable to query.
 * @param[out] value Pointer to the buffer to save the result to. This should not be allocated, and must be freed after use if this function returns IRECOVERY_E_SUCCESS.
 * @return An irecovery_error_t error code.
 * @see https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/include/libirecovery.h#L176
 */
irecovery_error_t irecovery_getenv(irecovery_client_t client, const char* variable, char** value);

/**
 * @brief Sets an environment variable's value on the device.
 * @param[in] client The client to send the request to.
 * @param[in] variable The variable to set.
 * @param[in] value The value to set the variable to.
 * @return An irecovery_error_t error code.
 * @see https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/include/libirecovery.h#L177
 */
irecovery_error_t irecovery_setenv(irecovery_client_t client, const char* variable, const char* value);

/**
 * @brief Sets an environment variable's value on the device.
 * @param[in] client The client to send the request to.
 * @param[in] variable The variable to set.
 * @param[in] value The value to set the variable to.
 * @return An irecovery_error_t error code.
 * @see https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/include/libirecovery.h#L178
 * @note If you don't know what the difference between setenv and setenvnp is, try regular setenv first.
 */
irecovery_error_t irecovery_setenv_np(irecovery_client_t client, const char* variable, const char* value);

/**
 * @brief Sends a reboot request to the device's console.
 * @param[in] client The client to send the request to.
 * @return An irecovery_error_t error code.
 * @see https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/include/libirecovery.h#L179
 */
irecovery_error_t irecovery_reboot(irecovery_client_t client);

/**
 * @brief Requests the on-device's return value.
 * @param[in] client The client to send the request to.
 * @param[out] value Pointer to save the value to.
 * @return An irecovery_error_t error code.
 * @see https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/include/libirecovery.h#L180
 */
irecovery_error_t irecovery_getret(irecovery_client_t client, unsigned int* value);

/**
 * @brief Gets the given client's device info.
 * @param[in] client The client to query.
 * @return Pointer to the client's device info.
 * @see https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/include/libirecovery.h#L184
 */
const struct irecovery_device_info* irecovery_get_device_info(irecovery_client_t client);

/**
 * @brief Gets a list of all Apple devices.
 * @return Pointer to an array of struct irecovery_device. Can be iterated over until struct members are NULL or -1.
 * @see https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/include/libirecovery.h#L187
 */
irecovery_device_t irecovery_devices_get_all(void);

/**
 * @brief Gets the device description for the given client.
 * @param[in] client The client to query.
 * @param[out] device Pointer to the device struct that the client matches.
 * @return An irecovery_error_t error code.
 * @see https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/include/libirecovery.h#L188
 */
irecovery_error_t irecovery_devices_get_device_by_client(irecovery_client_t client, irecovery_device_t* device);

/**
 * @brief Gets the device description for the given product type string.
 * @param[in] product_type The product type, case sensitive.
 * @param[out] device Pointer to the device struct that the product type matches.
 * @return An irecovery_error_t error code.
 * @see https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/include/libirecovery.h#L189
 */
irecovery_error_t irecovery_devices_get_device_by_product_type(const char* product_type, irecovery_device_t* device);

/**
 * @brief Gets the device description for the given hardware model string.
 * @param[in] hardware_model The hardware model, case sensitive.
 * @param[out] device Pointer to the device struct that the hardware model matches.
 * @return An irecovery_error_t error code.
 * @see https://github.com/libimobiledevice/libirecovery/blob/3fa36c5a7a745fd334bed8a3d5e432c626677910/include/libirecovery.h#L190
 */
irecovery_error_t irecovery_devices_get_device_by_hardware_model(const char* hardware_model, irecovery_device_t* device);

#endif