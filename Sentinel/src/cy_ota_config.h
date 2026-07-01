///
/// \file    cy_ota_config.h
/// \brief   OTA configuration parameters
///
/// \details Customer overrides for the OTA library. This file defines timing
///          parameters, retry counts, message templates, and MQTT settings
///          for Over-The-Air (OTA) firmware updates.
///
/// \author  galudino
/// \date    2025
/// \version 1.0 - OTA configuration definitions
///
/// \copyright Copyright 2024, Cypress Semiconductor Corporation
///            (an Infineon company)
///            SPDX-License-Identifier: Apache-2.0
///

#ifndef CY_OTA_CONFIG_H
#define CY_OTA_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/// \addtogroup group_ota_config OTA Configuration
/// {

//==============================================================================
// OTA Timing Configuration
//==============================================================================

///
/// \brief Initial time for checking for OTA updates
///
/// This is used to start the timer for the initial OTA update check after
/// calling cy_ota_agent_start().
///
#define CY_OTA_INITIAL_CHECK_SECS (10) ///< 10 seconds

///
/// \brief Next time for checking for OTA updates
///
/// This is used to restart the timer after an OTA update check in the OTA
/// Agent.
///
#define CY_OTA_NEXT_CHECK_INTERVAL_SECS (24 * 60 * 60) ///< 1 day between checks

///
/// \brief Retry time after which to check for OTA updates again
///
/// This is used to restart the timer after failing to contact the server
/// during an OTA update check.
///
#define CY_OTA_RETRY_INTERVAL_SECS                                             \
    (5) ///< 5 seconds between retries after error

///
/// \brief Length of time to check for downloads
///
/// The OTA Agent wakes up, connects to server, and waits this much time before
/// disconnecting. This allows the OTA Agent to be inactive for long periods of
/// time, only checking for short periods. Use 0x00 to continue checking once
/// started.
///
#define CY_OTA_CHECK_TIME_SECS (10 * 60) ///< 10 minutes

///
/// \brief Expected maximum download time between each OTA packet arrival
///
/// This is used to verify that the download occurs in a reasonable time frame.
/// Set to 0 to disable this check.
///
#define CY_OTA_PACKET_INTERVAL_SECS (0) ///< Default disabled

///
/// \brief Length of time to check for getting Job document
///
/// The OTA Agent wakes up, connects to broker/server, and waits this much time
/// before disconnecting. This allows the OTA Agent to be inactive for long
/// periods of time, only checking for short periods. Use 0x00 to continue
/// checking once started.
///
#define CY_OTA_JOB_CHECK_TIME_SECS (30) ///< 30 seconds

///
/// \brief Length of time to check for getting the OTA image data
///
/// After getting the Job (or during a direct download), this is the amount of
/// time to wait before canceling the download. Use 0x00 to disable.
///
#define CY_OTA_DATA_CHECK_TIME_SECS (20 * 60) ///< 20 minutes

//==============================================================================
// OTA Retry Configuration
//==============================================================================

///
/// \brief Number of retries when attempting an OTA update
///
/// This is used to determine the number of retries when attempting an OTA
/// update.
///
#define CY_OTA_RETRIES (3) ///< Retry entire process 3 times

///
/// \brief Number of retries when attempting to contact the server
///
/// This is used to determine the number of retries when connecting to the
/// server during an OTA update check.
///
#define CY_OTA_CONNECT_RETRIES (3) ///< 3 server connect retries

///
/// \brief Number of OTA download retries
///
/// Retry count for attempts at downloading the OTA image.
///
#define CY_OTA_MAX_DOWNLOAD_TRIES (3) ///< 3 download OTA image retries

//==============================================================================
// Message Topic Definitions
//==============================================================================

///
/// \brief Last part of the topic to subscribe
///
/// Topic for the device to send a message to the Publisher:
/// "COMPANY_TOPIC_PREPEND / BOARD_NAME / PUBLISHER_LISTEN_TOPIC"
/// The combined topic must match the Publisher's subscribe topic.
///
#define PUBLISHER_LISTEN_TOPIC "publish_notify"

///
/// \brief First part of the topic to subscribe/publish
///
/// Topic for the device to send a message to the Publisher:
/// "COMPANY_TOPIC_PREPEND / BOARD_NAME / PUBLISHER_LISTEN_TOPIC"
///
#define COMPANY_TOPIC_PREPEND "anycloud"

///
/// \brief End of topic to send a message to the Publisher for direct download
///
#define PUBLISHER_DIRECT_TOPIC "OTAImage"

///
/// \brief Update successful message
///
/// Used with sprintf() to create the RESULT message to the Broker/server.
///
#define CY_OTA_RESULT_SUCCESS "Success"

///
/// \brief Update failure message
///
/// Used with sprintf() to create the RESULT message to the Broker/server.
///
#define CY_OTA_RESULT_FAILURE "Failure"

///
/// \brief Default Job document name
///
/// Name of the update JSON file for HTTP.
///
#define CY_OTA_HTTP_JOB_FILE "/ota_update.json"

///
/// \brief Default OTA image file name
///
/// Name of the OTA image for HTTP.
///
#define CY_OTA_HTTP_DATA_FILE "/anycloud-ota.bin"

//==============================================================================
// Device Message Templates
//==============================================================================

///
/// \brief Device message to the Publisher to ask about updates
///
/// Used with sprintf() to insert the current version and UniqueTopicName at
/// runtime.
///
#define CY_OTA_SUBSCRIBE_UPDATES_AVAIL                                         \
    "{\
\"Message\":\"Update Availability\", \
\"Manufacturer\": \"Untitled\", \
\"ManufacturerID\": \"ABC\", \
\"ProductID\": \"ABC_UNT_123\", \
\"SerialNumber\": \"ABC213450001\", \
\"BoardName\": \"CYBLE-416045-EVAL\", \
\"Version\": \"%d.%d.%d\", \
\"UniqueTopicName\": \"%s\"\
}"

///
/// \brief Device message to the Publisher to ask for a full download
///
/// Used with sprintf() to insert values at runtime:
/// - Current Application Version
/// - UniqueTopicName
///
#define CY_OTA_DOWNLOAD_REQUEST                                                \
    "{\
\"Message\":\"Request Update\", \
\"Manufacturer\": \"Untitled\", \
\"ManufacturerID\": \"ABC\", \
\"ProductID\": \"ABC_UNT_123\", \
\"SerialNumber\": \"ABC213450001\", \
\"BoardName\": \"CYBLE-416045-EVAL\", \
\"Version\": \"%d.%d.%d\", \
\"UniqueTopicName\": \"%s\" \
}"

///
/// \brief Device message to the Publisher to ask for a chunk of data
///
/// Used with sprintf() to insert values at runtime:
/// - Current Application Version
/// - UniqueTopicName
/// - FileName
/// - Offset
/// - Size
///
#define CY_OTA_DOWNLOAD_CHUNK_REQUEST                                          \
    "{\
\"Message\":\"Request Data Chunk\", \
\"Manufacturer\": \"Untitled\", \
\"ManufacturerID\": \"ABC\", \
\"ProductID\": \"ABC_UNT_123\", \
\"SerialNumber\": \"ABC213450001\", \
\"BoardName\": \"CYBLE-416045-EVAL\", \
\"Version\": \"%d.%d.%d\", \
\"UniqueTopicName\": \"%s\", \
\"Filename\": \"%s\", \
\"Offset\": \"%ld\", \
\"Size\": \"%ld\"\
}"

///
/// \brief Device message to the Publisher to ask for a download
///
/// Used with sprintf() to insert the current version and UniqueTopicName at
/// runtime.
///
#define CY_OTA_DOWNLOAD_DIRECT_REQUEST                                         \
    "{\
\"Message\":\"Send Direct Update\", \
\"Manufacturer\": \"Untitled\", \
\"ManufacturerID\": \"ABC\", \
\"ProductID\": \"ABC_UNT_123\", \
\"SerialNumber\": \"ABC213450001\", \
\"BoardName\": \"CYBLE-416045-EVAL\", \
\"Version\": \"%d.%d.%d\" \
}"

///
/// \brief Device JSON document to respond to the MQTT Publisher
///
/// Used with sprintf() to create the JSON message.
///
#define CY_OTA_MQTT_RESULT_JSON                                                \
    "{\
\"Message\":\"%s\", \
\"UniqueTopicName\": \"%s\"\
}"

///
/// \brief Device JSON document to respond to the HTTP server
///
/// Used with sprintf() to create the JSON message.
///
#define CY_OTA_HTTP_RESULT_JSON                                                \
    "{\
\"Message\":\"%s\", \
\"File\":\"%s\" \
}"

//==============================================================================
// HTTP Template Definitions
//==============================================================================

///
/// \brief HTTP GET template
///
/// Used with sprintf() to create the GET request for the HTTP server.
///
#ifndef CY_OTA_HTTP_GET_TEMPLATE
#define CY_OTA_HTTP_GET_TEMPLATE                                               \
    "GET %s HTTP/1.1\r\n"                                                      \
    "Host: %s:%d \r\n"                                                         \
    "\r\n"
#endif

///
/// \brief HTTP GET Range template
///
/// Used with sprintf() to create the GET request for the HTTP server when
/// requesting a range of data.
///
#ifndef CY_OTA_HTTP_GET_RANGE_TEMPLATE
#define CY_OTA_HTTP_GET_RANGE_TEMPLATE                                         \
    "GET %s HTTP/1.1\r\n"                                                      \
    "Host: %s:%d \r\n"                                                         \
    "Range: bytes=%ld-%ld \r\n"                                                \
    "\r\n"
#endif

///
/// \brief HTTP POST template
///
/// Used with sprintf() to create the POST message for the HTTP server.
///
#ifndef CY_OTA_HTTP_POST_TEMPLATE
#define CY_OTA_HTTP_POST_TEMPLATE                                              \
    "POST %s HTTP/1.1\r\n"                                                     \
    "Content-Length:%ld \r\n"                                                  \
    "\r\n%s"
#endif

//==============================================================================
// MQTT Configuration
//==============================================================================

///
/// \brief The keepalive interval for MQTT
///
/// An MQTT ping request will be sent periodically at this interval.
///
#define CY_OTA_MQTT_KEEP_ALIVE_SECONDS (60) ///< 60 second keepalive

///
/// \brief Maximum number of MQTT Topics
///
/// The maximum number of Topics for subscribing.
///
#define CY_OTA_MQTT_MAX_TOPICS (2)

///
/// \brief MQTT topic prefix
///
/// Used as prefix for "Will" and "Acknowledgement" messages.
///
#define CY_OTA_MQTT_TOPIC_PREFIX "cy_ota_device"

///
/// \brief The first characters in the client identifier
///
/// A timestamp is appended to this prefix to create a unique client identifier
/// for each connection.
///
#define CY_OTA_MQTT_CLIENT_ID_PREFIX "cy_device"

/// \} group_ota_config
///

#ifdef __cplusplus
}
#endif

#endif /* CY_OTA_CONFIG_H */
