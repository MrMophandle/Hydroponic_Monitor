/**
 * wifi_conn — device-only station-mode Wi-Fi connectivity with capped
 * exponential-backoff reconnect and mDNS registration.
 *
 * Not host-testable (requires a real esp_wifi/esp_netif stack and a real
 * AP) per the Test Strategy for Phase 4 — see lib/wifi_backoff/ for the one
 * piece of this phase's logic (the backoff delay sequence) that IS pure and
 * host-tested.
 */
#ifndef HYDROPONIC_MONITOR_WIFI_CONN_H
#define HYDROPONIC_MONITOR_WIFI_CONN_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Starts Wi-Fi station-mode connectivity: initializes NVS (if not already
 * initialized), the network stack, the default event loop, and esp_wifi,
 * registers event handlers for connect/disconnect/got-ip, and calls
 * esp_wifi_start(). Non-blocking — wires everything and returns immediately;
 * the actual connect/reconnect sequence happens asynchronously via the
 * registered esp_event handlers. Call exactly once from app_main().
 *
 * Sampling is independent of connectivity by design (AC-ERROR-3): this must
 * be called without making sampler startup depend on its outcome.
 */
void wifi_conn_start(void);

#ifdef __cplusplus
}
#endif

#endif /* HYDROPONIC_MONITOR_WIFI_CONN_H */
