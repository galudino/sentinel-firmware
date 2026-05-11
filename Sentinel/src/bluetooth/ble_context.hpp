///
/// \file    ble_context.hpp
/// \brief   Bluetooth LE public interface
///
/// \details This header provides the public interface for Bluetooth LE
///          functionality including initialization and stack management.
///
/// \author  galudino
/// \date    2025
/// \version 1.0 - BLE module interface
///

#ifndef BLE_CONTEXT_HPP
#define BLE_CONTEXT_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cy_ota_api.h"

#include "cycfg_gatt_db.h"
#include "wiced_bt_ble.h"
#include "wiced_bt_dev.h"
#include "wiced_bt_gatt.h"
#include "wiced_bt_stack.h"
}
#pragma GCC diagnostic pop

#include <array>

///
/// \brief Application context structure for BLE/OTA operations
///
/// This structure maintains all state information for the OTA application,
/// including OTA context, connection details, and BLE parameters.
///
/// Default values are set on acquisition of the context.
///
class ble_context final {
public:
    ///
    /// \brief Initialize the Bluetooth LE stack
    ///
    /// Initializes context defaults, configures the Bluetooth platform, and
    /// initializes the WICED BT stack with the management callback. This is the
    /// entry point for BLE functionality.
    ///
    /// \return wiced_result_t WICED_BT_SUCCESS if initialization succeeded,
    ///         error code otherwise. Failure triggers an assertion.
    ///
    wiced_result_t stack_initialize() noexcept;

    ///
    /// \brief Deinitializes the Bluetooth LE stack
    ///
    /// \return wiced_result_t WICED_BT_SUCCESS if deinitialization succeeded,
    ///         error code otherwise. Failure triggers an assertion.
    ///
    wiced_result_t stack_deinitialize() noexcept {
        return wiced_bt_stack_deinit();
    }

    ///
    /// \brief Get current Bluetooth connection ID
    ///
    /// \return uint16_t Current connection ID
    ///
    uint16_t connection_id() const noexcept { return m_connection_id; }

    bool connected() const noexcept { return m_connection_id > 0; }

    ///
    /// \brief Handle BLE connection and disconnection events
    ///
    /// Updates connection state, stores peer address on connection, and
    /// restarts advertising on disconnection. Updates the advertising LED to
    /// reflect the current state.
    ///
    /// \param connection_status Pointer to connection status structure
    /// containing
    ///        connection state, connection ID, and peer address
    ///
    /// \return wiced_bt_gatt_status_t WICED_BT_GATT_SUCCESS if handled
    /// successfully,
    ///         WICED_BT_GATT_ERROR if connection_status is null. Assertion
    ///         triggered if advertising restart fails after disconnection.
    ///
    wiced_bt_gatt_status_t connection_event_handler(
        wiced_bt_gatt_connection_status_t *connection_status);

    ///
    /// \brief Update advertising LED based on current state
    ///
    /// Controls the PWM duty cycle of the advertising LED to indicate the
    /// current advertising and connection state:
    /// - Off: Not advertising, not connected
    /// - Blinking: Advertising, not connected
    /// - On: Connected
    ///
    /// \return cy_rslt_t CY_RSLT_SUCCESS if PWM operations succeeded,
    ///         error code otherwise
    ///
    cy_rslt_t update_advertising_led() noexcept;

    ///
    /// \brief Set advertising/connection state
    ///
    /// \param advertisement_mode Pointer to advertisement mode
    ///
    void set_advertising_mode(
        wiced_bt_ble_advert_mode_t *advertisement_mode) noexcept {
        m_connection_state =
            *advertisement_mode ==
                    wiced_bt_ble_advert_mode_e::BTM_BLE_ADVERT_OFF
                ? (m_connection_id == 0 ? state::disconnected_not_advertising
                                        : state::connected)
                : state::disconnected_and_advertising;
    }

    ///
    /// \brief Initialize and start the OTA agent
    ///
    /// Validates the context tag, initializes OTA network and agent parameters,
    /// and starts the OTA agent. Enables post-reboot validation to support OTA
    /// revert functionality. Enters infinite loop on failure.
    ///
    /// \return cy_rslt_t CY_RSLT_SUCCESS if OTA agent started successfully,
    ///         CY_RSLT_OTA_ERROR_BADARG if context tag is invalid,
    ///         or does not return (infinite loop) if agent start fails
    ///
    cy_rslt_t ota_agent_initialize() noexcept;

    ///
    /// \brief Handle GATT write requests for OTA operations
    ///
    /// Processes GATT write requests for OTA control point, data transfer, and
    /// client characteristic configuration. Supports OTA commands including
    /// prepare download, download, verify, and abort operations.
    ///
    /// \param event_data Pointer to GATT event data containing write request
    /// details
    /// \param error_handle Pointer to error handle, set to the attribute handle
    ///        that caused an error for error reporting
    ///
    /// \return wiced_bt_gatt_status_t WICED_BT_GATT_SUCCESS if handled
    /// successfully,
    ///         WICED_BT_GATT_ERROR if operation failed,
    ///         WICED_BT_GATT_REQ_NOT_SUPPORTED for unsupported operations
    ///
    wiced_bt_gatt_status_t
    ota_agent_write_handler(wiced_bt_gatt_event_data_t *event_data,
                            uint16_t *error_handle) noexcept;

    ///
    /// \brief Handle OTA operation confirmation
    ///
    /// Called after an OTA operation completes. Checks the OTA library state
    /// and either reboots the device (if configured and OTA is complete) or
    /// stops the OTA agent. Provides a 1-second delay before reboot to allow
    /// final operations to complete.
    ///
    void ota_agent_confirmation_handler() noexcept;

private:
    ///
    /// \brief Advertising and connection state enumeration
    ///
    /// Defines the possible states combining advertising and connection status
    /// for LED indication and state management.
    ///
    enum class state : uint8_t {
        disconnected_not_advertising,
        disconnected_and_advertising,
        connected
    };

    uint32_t m_tag; ///< Context validity tag for integrity checking

    uint16_t m_connection_id; ///< Current BLE connection ID (0 if disconnected)

    std::array<uint8_t, BD_ADDR_LEN>
        m_peer_address; ///< Bluetooth address of connected peer

    wiced_bt_ble_conn_params_t
        m_connection_parameters; ///< BLE connection parameters

    state m_connection_state; ///< Current advertising and connection state

    cy_ota_context_ptr m_ota_context;      ///< OTA library context pointer
    cy_ota_connection_t m_connection_type; ///< Connection type for OTA (BLE)

    bool m_reboot_at_end; ///< Reboot flag after OTA completion:
                          ///< false = no reboot,
                          ///< true = reboot after successful OTA

    uint16_t m_ota_config_descriptor; ///< OTA config descriptor for
                                      ///< notifications/indications

    cy_ota_agent_params_t
        m_ota_agent_params; ///< OTA agent configuration parameters
    cy_ota_network_params_t
        m_ota_network_params; ///< OTA network configuration parameters

    ///
    /// \brief Initialize BLE context with default values
    ///
    /// Sets all member variables to their default initial state. Called during
    /// stack initialization to ensure a clean starting state. Configures BLE as
    /// the OTA connection type and enables automatic reboot after successful
    /// OTA.
    ///
    void default_value_initialize() noexcept {
        m_tag = BLE_CONTEXT_TAG_VALID;

        m_connection_id = 0;
        m_connection_parameters = {};
        m_connection_state = state::disconnected_not_advertising;
    }

    void ota_value_initialize() noexcept {
        // Will be assigned from cy_ota_agent_start() function call
        m_ota_context = nullptr;

        m_connection_type = cy_ota_connection_t::CY_OTA_CONNECTION_BLE;
        m_reboot_at_end = true;

        m_ota_config_descriptor = {};

        // OTA Agent parameters - used for ALL transport types
        m_ota_agent_params = {
            true,    // Reboot after finishing OTA update
            true,    // Validate software after reboot
            false,   // Will send result after OTA update
            nullptr, // TODO: create callback function
            nullptr  // TODO: create argument for callback function
        };

        // Common Network Parameters
        m_ota_network_params = {m_connection_type,
                                cy_ota_update_flow_t::CY_OTA_JOB_FLOW};
    }

    ///
    /// \brief Bluetooth stack management callback
    ///
    /// Static callback function handling Bluetooth stack management events.
    /// Processes events including stack enable, pairing, security, encryption,
    /// and advertising state changes. Automatically starts advertising when the
    /// stack is enabled.
    ///
    /// \param event Management event type from the Bluetooth stack
    /// \param event_data Pointer to event-specific data structure
    ///
    /// \return wiced_bt_dev_status_t WICED_BT_SUCCESS if event handled
    /// successfully,
    ///         WICED_BT_ERROR if event not handled or invalid
    ///
    static wiced_bt_dev_status_t stack_management_callback(
        wiced_bt_management_evt_t event,
        wiced_bt_management_evt_data_t *event_data) noexcept;

    /// Magic number indicating valid/initialized context
    static constexpr auto OTA_APP_TAG_VALID = uint32_t{0x51EDBA15};

    /// Magic number indicating valid BLE context state
    static constexpr auto BLE_CONTEXT_TAG_VALID = uint32_t{0x51EDBA15};

    /// Magic number indicating invalid/uninitialized BLE context state
    static constexpr auto BLE_CONTEXT_TAG_INVALID = uint32_t{0xDEADBEEF};
};

///
/// \brief Global BLE/OTA application context instance
///
/// This global context object must be accessible to ble_gatt.cpp and all tasks
/// throughout the application. It maintains the state of BLE connections,
/// advertising, and OTA operations.
///
inline auto ble_context_object = ble_context{};

#endif /* BLE_CONTEXT_HPP */
