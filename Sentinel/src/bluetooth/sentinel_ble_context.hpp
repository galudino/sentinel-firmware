///
/// \file    sentinel_ble_context.hpp
/// \brief   Bluetooth LE public interface
///
/// \details This header provides the public interface for Bluetooth LE
///          functionality including initialization and stack management.
///
/// \author  galudino
/// \date    2021-2024
/// \version 1.0 - BLE module interface
///

#ifndef SENTINEL_BLE_CONTEXT_HPP
#define SENTINEL_BLE_CONTEXT_HPP

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

#include "sentinel_firmware_version.hpp"

#include <array>
#include <cstdint>

namespace sentinel {

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

    /// \brief \c true while a central is connected.
    /// \return \c true if \ref connection_id is nonzero.
    bool connected() const noexcept { return m_connection_id > 0; }

    ///
    /// \brief \c true once the GATT database registered successfully (#6).
    ///
    /// \details Set from \c ble_start_advertising after \c
    /// wiced_bt_gatt_db_init.
    ///          The boot orchestrator reads this live for the POST \c gatt_db
    ///          probe; by POST time (after the multi-second flash-store scan)
    ///          the asynchronous \c BTM_ENABLED registration has already run.
    ///
    /// \return \c true once the GATT database has registered successfully.
    ///
    bool gatt_db_ok() const noexcept { return m_gatt_db_ok; }

    /// \brief Record the GATT-DB registration result (called at registration).
    /// \param ok \c true if the GATT database registered successfully.
    void set_gatt_db_ok(bool ok) noexcept { m_gatt_db_ok = ok; }

    /// \brief Last cached peer RSSI in dBm (negative; 0 if not yet read). #6
    /// \return The cached peer RSSI in dBm.
    int8_t peer_rssi() const noexcept { return m_peer_rssi; }

    /// \brief Last cached connection TX power in dBm (0 if not yet read). #6
    /// \return The cached connection TX power in dBm.
    int8_t tx_power_dbm() const noexcept { return m_tx_power_dbm; }

    ///
    /// \brief Kick asynchronous reads of peer RSSI + connection TX power.
    ///
    /// \details Non-blocking: issues the HCI reads (throttled to ~1 Hz) and
    ///          returns immediately; the results land in \ref m_peer_rssi /
    ///          \ref m_tx_power_dbm via the completion callbacks. Callers read
    ///          the \e previously cached value (\ref peer_rssi / \ref
    ///          tx_power_dbm). A no-op when not connected. Feeds the
    ///          \c device_snapshot BLE fields (#6/#36).
    ///
    void refresh_link_metrics() noexcept;

    /// \brief Cache a freshly read peer RSSI (from the read-RSSI callback).
    /// \param rssi Freshly read peer RSSI in dBm.
    void set_peer_rssi(int8_t rssi) noexcept { m_peer_rssi = rssi; }

    /// \brief Cache a freshly read TX power (from the read-TX-power callback).
    /// \param dbm Freshly read connection TX power in dBm.
    void set_tx_power_dbm(int8_t dbm) noexcept { m_tx_power_dbm = dbm; }

    ///
    /// \brief Handle BLE connection and disconnection events
    ///
    /// Updates connection state, stores peer address on connection, and
    /// restarts advertising on disconnection. Updates the advertising LED to
    /// reflect the current state.
    ///
    /// \param connection_status Reference to the connection status structure
    ///        containing connection state, connection ID, and peer address.
    ///
    /// \return wiced_bt_gatt_status_t WICED_BT_GATT_SUCCESS if handled
    ///         successfully. Assertion triggered if advertising restart fails
    ///         after disconnection.
    ///
    wiced_bt_gatt_status_t connection_event_handler(
        const wiced_bt_gatt_connection_status_t &connection_status);

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
    /// \param advertisement_mode The current advertisement mode.
    ///
    void set_advertising_mode(
        wiced_bt_ble_advert_mode_t advertisement_mode) noexcept {
        m_connection_state =
            advertisement_mode == wiced_bt_ble_advert_mode_e::BTM_BLE_ADVERT_OFF
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
    /// \param error_handle Reference to error handle, set to the attribute
    /// handle
    ///        that caused an error for error reporting
    ///
    /// \return wiced_bt_gatt_status_t WICED_BT_GATT_SUCCESS if handled
    /// successfully,
    ///         WICED_BT_GATT_ERROR if operation failed,
    ///         WICED_BT_GATT_REQ_NOT_SUPPORTED for unsupported operations
    ///
    wiced_bt_gatt_status_t
    ota_agent_write_handler(wiced_bt_gatt_event_data_t *event_data,
                            uint16_t &error_handle) noexcept;

    ///
    /// \brief Handle OTA operation confirmation
    ///
    /// Called after an OTA operation completes. Checks the OTA library state
    /// and either reboots the device (if configured and OTA is complete) or
    /// stops the OTA agent. Provides a 1-second delay before reboot to allow
    /// final operations to complete.
    ///
    void ota_agent_confirmation_handler() noexcept;

    ///
    /// \brief Stop the OTA agent and drop the context (#83).
    ///
    /// \details Idempotent — safe to call when no OTA is active (no-op). Called
    ///          on every terminal OTA path (abort, verify-fail, in-flight
    ///          disconnect, and before a fresh start) so a later PREPARE does
    ///          not hit "OTA context already created!".
    ///
    void ota_cleanup() noexcept;

    ///
    /// \brief Log a failed/interrupted OTA and reset the device to a clean state.
    ///
    /// \details The deterministic recovery for a *failed* update (signal loss
    ///          mid-download, image verification failure, or an agent start
    ///          failure): record a \c firmware_update_failed event with \p reason,
    ///          tear the OTA agent down, then \c NVIC_SystemReset() after a short
    ///          delay so the log flush and any in-flight BLE indication settle.
    ///          The reboot brings every task + GATT service back fresh and the
    ///          board re-advertises — the client then reconnects to a known-good
    ///          device. A deliberate user *cancel* is NOT a failure and does not
    ///          reset (it stays connected via \ref ota_cleanup).
    ///
    /// \param reason The \c firmware_update_record.mcuboot_result reason code
    ///               (\c OTA_FAIL_SIGNAL_LOST / \c OTA_FAIL_VERIFY /
    ///               \c OTA_FAIL_START). Does not return.
    ///
    [[noreturn]] void ota_fail_and_reset(uint32_t reason) noexcept;

    ///
    /// \brief Reset the per-OTA attempt bookkeeping (#86) — buffered header
    ///        bytes, the parsed target version, and the attempted-logged flag.
    ///        Called at PREPARE, before the agent starts.
    ///
    void ota_reset_attempt_state() noexcept;

    ///
    /// \brief Buffer the leading bytes of the downloaded image until the
    ///        MCUBoot header's version field is in hand, then parse the target
    ///        version and write the \c firmware_update_attempted record (#86).
    ///
    /// \details Tolerates a header split across several data writes. A header
    ///          whose magic does not match leaves the target unknown (0.0.0.0)
    ///          but still writes the attempted record.
    ///
    /// \param data Bytes of one image data write.
    /// \param len  Number of bytes in \p data.
    ///
    void ota_capture_header(const uint8_t *data, uint16_t len) noexcept;

    ///
    /// \brief Write this OTA's \c firmware_update_attempted record exactly once
    ///        (#86): with the parsed target if the header has arrived, else
    ///        0.0.0.0. Called when the header parses and before any failure
    ///        record, so every OTA still logs attempted → completed | failed.
    ///
    void ota_log_attempted_once() noexcept;

    ///
    /// \brief The target version parsed from the image header (#86).
    ///
    /// \return The target, or 0.0.0.0 if the header has not arrived or did
    ///         not parse.
    ///
    sentinel::firmware_version ota_target_version() const noexcept;

    ///
    /// \brief Get the current negotiated MTU value
    ///
    /// \return uint16_t Current MTU value
    ///
    uint16_t mtu() const noexcept { return m_mtu; }

    ///
    /// \brief Set the current negotiated MTU value
    ///
    /// \param mtu New MTU value to set
    ///
    void set_mtu(uint16_t mtu) noexcept { m_mtu = mtu; }

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

    bool m_gatt_db_ok{false}; ///< GATT-DB registration result (#6).

    volatile int8_t m_peer_rssi{0};    ///< Last cached peer RSSI, dBm (#6).
    volatile int8_t m_tx_power_dbm{0}; ///< Last cached TX power, dBm (#6).
    uint32_t m_last_metrics_tick{0};   ///< Throttle for refresh_link_metrics.

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

    /// MCUBoot image header magic (\c ih_magic, little-endian u32 at offset 0).
    static constexpr uint32_t MCUBOOT_IMAGE_MAGIC_LE = 0x96F3B83Du;
    /// Offset of \c ih_ver in the MCUBoot image header.
    static constexpr uint32_t MCUBOOT_HDR_VER_OFFSET = 0x14u;
    /// Header bytes needed to read \c ih_ver (offset + its 8 bytes).
    static constexpr uint32_t MCUBOOT_HDR_NEEDED = 0x1Cu;

    std::array<uint8_t, MCUBOOT_HDR_NEEDED>
        m_ota_header{};                ///< #86: leading image bytes.
    uint32_t m_ota_header_len{0};      ///< #86: bytes buffered so far.
    bool m_ota_attempt_logged{false};  ///< #86: attempted written this OTA.
    bool m_ota_target_known{false};    ///< #86: target parsed from header.
    uint8_t m_ota_target_major{0};     ///< #86: target major.
    uint8_t m_ota_target_minor{0};     ///< #86: target minor.
    uint8_t m_ota_target_patch{0};     ///< #86: target patch (iv_revision).
    uint16_t m_ota_target_build{0};    ///< #86: target build (iv_build_num).

    uint16_t m_mtu; ///< Negotiated MTU value for BLE notifications (default 23,
                    ///< updated on connection)

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

        m_mtu = 23; // Default MTU before negotiation

        // OTA control-point CCCD (notify/indicate flags). Initialized once here
        // at stack bring-up; thereafter it is owned by the client's CCCD write
        // and must survive until the PREPARE command reads it (see the note in
        // ota_value_initialize()).
        m_ota_config_descriptor = {};
    }

    ///
    /// \brief Initialize OTA-related member state to its default values.
    ///
    /// \details Sets the OTA connection type to BLE, enables automatic reboot
    ///          after a successful OTA, and resets the OTA context/descriptor.
    ///
    void ota_value_initialize() noexcept {
        // Will be assigned from cy_ota_agent_start() function call
        m_ota_context = nullptr;

        m_connection_type = cy_ota_connection_t::CY_OTA_CONNECTION_BLE;
        m_reboot_at_end = true;

        // NOTE: m_ota_config_descriptor is intentionally NOT reset here. The
        // client enables the control-point CCCD (notify/indicate) BEFORE it
        // writes the PREPARE command, and this function runs at the *start* of
        // PREPARE (via ota_agent_initialize()). Resetting it here clobbered the
        // descriptor the client just set, so cy_ota_ble_download_prepare()
        // received 0x0, its prepare-response indication failed ("Unknown BT
        // descriptor 0x0" -> 0x8787), and the OTA aborted at 0% (#63). It is
        // initialized once at stack bring-up in default_value_initialize().

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

    /// Preferred connection interval requested on connect (#76): 12 × 1.25 ms
    /// = 15 ms, the shortest Apple's accessory guidelines allow.
    static constexpr auto PREFERRED_CONN_INTERVAL = uint16_t{12};

    /// Preferred peripheral latency (connection events the peripheral may
    /// skip): 0, so every request is answered on the next event.
    static constexpr auto PREFERRED_CONN_LATENCY = uint16_t{0};

    /// Preferred supervision timeout, in 10 ms units: 5 s.
    static constexpr auto PREFERRED_SUPERVISION_TIMEOUT = uint16_t{500};
};

///
/// \brief Global BLE/OTA application context instance
///
/// This global context object must be accessible to ble_gatt.cpp and all tasks
/// throughout the application. It maintains the state of BLE connections,
/// advertising, and OTA operations.
///
inline auto ble_context_object = ble_context{};

} // namespace sentinel

#endif /* SENTINEL_BLE_CONTEXT_HPP */
