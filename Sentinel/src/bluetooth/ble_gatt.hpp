///
/// \file    ble_gatt.hpp
/// \brief   Bluetooth LE public interface for GATT operations
///
/// \details This header provides the public interface for Bluetooth LE
///          functionality including GATT database operations and event
///          handling.
///
/// \author  galudino
/// \date    2025
/// \version 1.0 - BLE module interface
///

#ifndef BLE_GATT_HPP
#define BLE_GATT_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cy_ota_api.h"

#include "cycfg_gatt_db.h"
#include "wiced_bt_ble.h"
#include "wiced_bt_gatt.h"
}
#pragma GCC diagnostic pop

///
/// \brief Set value in GATT database
///
/// Updates the value and current length of an attribute in the GATT database.
/// If the new length is smaller than the maximum length, the remaining buffer
/// is zeroed for deterministic BLE reads of variable-length characteristics.
///
/// \param attr_handle Attribute handle to update
/// \param value Pointer to new value data (must not be NULL if length > 0)
/// \param length Length of new value data in bytes
///
/// \return wiced_bt_gatt_status_t WICED_BT_GATT_SUCCESS if value set
///         successfully, WICED_BT_GATT_INVALID_HANDLE if handle not found,
///         WICED_BT_GATT_INVALID_ATTR_LEN if length exceeds maximum,
///         WICED_BT_GATT_INVALID_PDU if value is NULL when length > 0,
///         WICED_BT_GATT_ERROR if attribute data pointer is NULL
///
wiced_bt_gatt_status_t ble_gatt_db_set_value(uint16_t attr_handle,
                                             uint8_t *value, uint16_t length);

///
/// \brief Find GATT attribute by handle
///
/// Searches the GATT database lookup table for an entry matching the specified
/// attribute handle using a linear search algorithm.
///
/// \param handle Attribute handle to search for
///
/// \return gatt_db_lookup_table_t* Pointer to matching attribute entry,
///         or NULL if handle not found in database
///
gatt_db_lookup_table_t *ble_gatt_db_find_by_handle(uint16_t handle);

///
/// \brief Main GATT event callback
///
/// Primary callback function registered with the Bluetooth stack to handle all
/// GATT events. Routes connection events, attribute requests, buffer
/// management, and transmission events to appropriate handlers.
///
/// \param event GATT event type (connection status, attribute request, buffer
///        request, transmission complete, etc.)
/// \param event_data Pointer to event-specific data structure
///
/// \return wiced_bt_gatt_status_t WICED_BT_GATT_SUCCESS if event handled
///         successfully, or error code from event handlers on failure
///
wiced_bt_gatt_status_t
ble_gatt_event_callback(wiced_bt_gatt_evt_t event,
                        wiced_bt_gatt_event_data_t *event_data);

///
/// \brief GATT server request event handler
///
/// Processes GATT attribute requests by routing to specific handlers based on
/// operation code (read, write, MTU exchange, etc.). Automatically sends error
/// responses for failed operations via the callback mechanism.
///
/// \param event_data Pointer to GATT event data containing attribute request
///        details including operation code, connection ID, and request
///        parameters
/// \param error_handle Pointer to variable that receives the attribute handle
///        that caused an error, used for error response generation
///
/// \return wiced_bt_gatt_status_t WICED_BT_GATT_SUCCESS if request handled
///         successfully, or appropriate error code indicating failure reason
///
wiced_bt_gatt_status_t
ble_gatt_event_handler(wiced_bt_gatt_event_data_t *event_data,
                       uint16_t *error_handle);

///
/// \brief Handle GATT read request
///
/// Processes GATT_REQ_READ and GATT_REQ_READ_BLOB operations. Validates the
/// requested attribute handle, checks offset bounds, and sends the requested
/// attribute data back to the client. Supports partial reads via offset
/// parameter.
///
/// \param connection_id BLE connection identifier
/// \param opcode GATT operation code (GATT_REQ_READ or GATT_REQ_READ_BLOB)
/// \param read_request Pointer to read request structure containing handle and
///        offset information
/// \param length_requested Maximum length of data that can be returned
/// \param error_handle Pointer to variable that receives the handle causing an
///        error for error response generation
///
/// \return wiced_bt_gatt_status_t WICED_BT_GATT_SUCCESS if read successful,
///         WICED_BT_GATT_INVALID_HANDLE if attribute not found,
///         WICED_BT_GATT_INVALID_OFFSET if offset exceeds attribute length
///
wiced_bt_gatt_status_t ble_gatt_request_read_handler(
    uint16_t connection_id, wiced_bt_gatt_opcode_t opcode,
    wiced_bt_gatt_read_t *read_request, uint16_t length_requested,
    uint16_t *error_handle);

///
/// \brief Handle GATT read by type request
///
/// Processes GATT_REQ_READ_BY_TYPE operations. Searches for all attributes
/// within the specified handle range that match the requested UUID type,
/// constructs a response containing handle-value pairs, and sends it to the
/// client. Allocates dynamic memory for response buffer.
///
/// \param connection_id BLE connection identifier
/// \param opcode GATT operation code (GATT_REQ_READ_BY_TYPE)
/// \param read_request Pointer to read by type request containing handle range
///        (s_handle to e_handle) and UUID type to match
/// \param length_requested Maximum length of data that can be returned
/// \param error_handle Pointer to variable that receives the handle causing an
///        error for error response generation
///
/// \return wiced_bt_gatt_status_t WICED_BT_GATT_SUCCESS if read successful,
///         WICED_BT_GATT_INVALID_HANDLE if no matching attributes found,
///         WICED_BT_GATT_INSUF_RESOURCE if memory allocation fails
///
wiced_bt_gatt_status_t ble_gatt_request_read_by_type_handler(
    uint16_t connection_id, wiced_bt_gatt_opcode_t opcode,
    wiced_bt_gatt_read_by_type_t *read_request, uint16_t length_requested,
    uint16_t *error_handle);

///
/// \brief Handle GATT read multiple request
///
/// Processes GATT_REQ_READ_MULTI and GATT_REQ_READ_MULTI_VAR_LENGTH operations.
/// Reads multiple attributes in a single request by iterating through the
/// provided handle list and concatenating their values into a single response
/// buffer. Allocates dynamic memory for response.
///
/// \param connection_id BLE connection identifier
/// \param opcode GATT operation code (GATT_REQ_READ_MULTI or
///        GATT_REQ_READ_MULTI_VAR_LENGTH)
/// \param read_multiple_request Pointer to read multiple request containing
///        handle stream and count of handles to read
/// \param length_requested Maximum length of data that can be returned
/// \param error_handle Pointer to variable that receives the handle causing an
///        error for error response generation
///
/// \return wiced_bt_gatt_status_t WICED_BT_GATT_SUCCESS if read successful,
///         WICED_BT_GATT_INVALID_HANDLE if any handle not found or memory
///         allocation fails, WICED_BT_GATT_ERR_UNLIKELY if attribute lookup
///         fails
///
wiced_bt_gatt_status_t ble_gatt_request_read_multi_handler(
    uint16_t connection_id, wiced_bt_gatt_opcode_t opcode,
    wiced_bt_gatt_read_multiple_req_t *read_multiple_request,
    uint16_t length_requested, uint16_t *error_handle);

///
/// \brief Handle GATT write request
///
/// Processes GATT_REQ_WRITE, GATT_CMD_WRITE, and GATT_CMD_SIGNED_WRITE
/// operations. Routes OTA-specific writes to the OTA handler, and all other
/// writes to the database update function. Automatically sends write response
/// for GATT_REQ_WRITE operations on success.
///
/// \param event_data Pointer to GATT event data containing write request with
///        handle, value, and length information
/// \param error_handle Pointer to variable that receives the handle causing an
///        error for error response generation
///
/// \return wiced_bt_gatt_status_t WICED_BT_GATT_SUCCESS if write successful,
///         or error code from OTA handler or database update function
///
wiced_bt_gatt_status_t
ble_gatt_command_write_handler(wiced_bt_gatt_event_data_t *event_data,
                               uint16_t *error_handle);

#endif /* BLE_GATT_HPP */
