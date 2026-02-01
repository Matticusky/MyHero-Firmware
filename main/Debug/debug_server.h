#ifndef DEBUG_SERVER_H
#define DEBUG_SERVER_H

#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize WiFi and start debug web server
 *
 * Connects to WiFi and starts HTTP server for file management.
 *
 * @return ESP_OK on success
 */
esp_err_t debug_server_start(void);

/**
 * @brief Stop debug web server and disconnect WiFi
 *
 * @return ESP_OK on success
 */
esp_err_t debug_server_stop(void);

/**
 * @brief Check if debug server is running
 *
 * @return true if server is running
 */
bool debug_server_is_running(void);

#ifdef __cplusplus
}
#endif

#endif // DEBUG_SERVER_H
