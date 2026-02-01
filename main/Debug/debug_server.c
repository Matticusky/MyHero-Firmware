#include "debug_server.h"
#include "../Storage/storage.h"

#include <string.h>

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_http_server.h>
#include <nvs_flash.h>

static const char *TAG = "DebugServer";

// WiFi credentials
#define WIFI_SSID      "Default"
#define WIFI_PASSWORD  "systemtools"
#define WIFI_MAX_RETRY 10

// Event group for WiFi connection
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static httpd_handle_t server = NULL;
static bool is_running = false;
static int wifi_retry_count = 0;

// HTML page template
static const char *HTML_HEADER =
    "<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>MyHero Debug</title>"
    "<style>"
    "body{font-family:Arial,sans-serif;margin:20px;background:#f5f5f5;}"
    "h1{color:#333;}.container{max-width:800px;margin:0 auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1);}"
    ".file-list{list-style:none;padding:0;}"
    ".file-item{display:flex;justify-content:space-between;align-items:center;padding:10px;border-bottom:1px solid #eee;}"
    ".file-item:hover{background:#f9f9f9;}"
    ".file-name{font-weight:bold;}"
    ".file-size{color:#666;font-size:0.9em;}"
    ".btn{padding:8px 16px;border:none;border-radius:4px;cursor:pointer;text-decoration:none;}"
    ".btn-download{background:#4CAF50;color:white;}"
    ".btn-delete{background:#f44336;color:white;}"
    ".upload-form{margin-top:20px;padding:20px;background:#e8f5e9;border-radius:8px;}"
    ".upload-form input[type=file]{margin:10px 0;}"
    ".btn-upload{background:#2196F3;color:white;}"
    ".info{margin-top:20px;padding:10px;background:#e3f2fd;border-radius:4px;font-size:0.9em;}"
    "</style></head><body><div class='container'>";

static const char *HTML_FOOTER = "</div></body></html>";

// WiFi event handler
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (wifi_retry_count < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            wifi_retry_count++;
            ESP_LOGI(TAG, "Retrying WiFi connection (%d/%d)", wifi_retry_count, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "WiFi connection failed after %d retries", WIFI_MAX_RETRY);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_retry_count = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// Format file size for display
static void format_size(size_t size, char *buf, size_t buf_size)
{
    if (size < 1024) {
        snprintf(buf, buf_size, "%zu B", size);
    } else if (size < 1024 * 1024) {
        snprintf(buf, buf_size, "%.1f KB", size / 1024.0);
    } else {
        snprintf(buf, buf_size, "%.1f MB", size / (1024.0 * 1024.0));
    }
}

// HTTP handler: Main page with file list
static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, HTML_HEADER);
    httpd_resp_sendstr_chunk(req, "<h1>MyHero Debug Server</h1>");

    // File list
    httpd_resp_sendstr_chunk(req, "<h2>Files on Storage</h2><ul class='file-list'>");

    char base_path[32];
    get_base_path(base_path, sizeof(base_path));

    DIR *dir = opendir(base_path);
    if (dir) {
        struct dirent *entry;
        char full_path[128];
        struct stat st;
        char size_str[32];
        char item_html[1024];

        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_DIR) continue;
            if (strlen(entry->d_name) > 64) continue;  // Skip overly long names

            snprintf(full_path, sizeof(full_path), "%s/%.64s", base_path, entry->d_name);
            if (stat(full_path, &st) == 0) {
                format_size(st.st_size, size_str, sizeof(size_str));
                snprintf(item_html, sizeof(item_html),
                    "<li class='file-item'>"
                    "<div><span class='file-name'>%.64s</span><br><span class='file-size'>%s</span></div>"
                    "<div>"
                    "<a href='/download?file=%.64s' class='btn btn-download'>Download</a> "
                    "<a href='/delete?file=%.64s' class='btn btn-delete' onclick=\"return confirm('Delete %.64s?')\">Delete</a>"
                    "</div></li>",
                    entry->d_name, size_str, entry->d_name, entry->d_name, entry->d_name);
                httpd_resp_sendstr_chunk(req, item_html);
            }
        }
        closedir(dir);
    }

    httpd_resp_sendstr_chunk(req, "</ul>");

    // Upload form
    httpd_resp_sendstr_chunk(req,
        "<div class='upload-form'>"
        "<h3>Upload File</h3>"
        "<form action='/upload' method='post' enctype='multipart/form-data'>"
        "<input type='file' name='file' accept='.wav,.aac,.mp3'><br>"
        "<button type='submit' class='btn btn-upload'>Upload</button>"
        "</form></div>");

    // Storage info
    size_t total, used, free_space;
    get_storage_info(&total, &used, &free_space);
    char info_html[256];
    char total_str[32], used_str[32], free_str[32];
    format_size(total, total_str, sizeof(total_str));
    format_size(used, used_str, sizeof(used_str));
    format_size(free_space, free_str, sizeof(free_str));
    snprintf(info_html, sizeof(info_html),
        "<div class='info'><strong>Storage:</strong> %s used / %s total (%s free)</div>",
        used_str, total_str, free_str);
    httpd_resp_sendstr_chunk(req, info_html);

    httpd_resp_sendstr_chunk(req, HTML_FOOTER);
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

// HTTP handler: Download file
static esp_err_t download_handler(httpd_req_t *req)
{
    char query[128];
    char filename[64];

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing filename");
        return ESP_FAIL;
    }

    if (httpd_query_key_value(query, "file", filename, sizeof(filename)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename");
        return ESP_FAIL;
    }

    char base_path[32];
    get_base_path(base_path, sizeof(base_path));
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%s", base_path, filename);

    FILE *file = fopen(full_path, "rb");
    if (!file) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    // Set content type based on extension
    const char *ext = strrchr(filename, '.');
    if (ext && strcasecmp(ext, ".wav") == 0) {
        httpd_resp_set_type(req, "audio/wav");
    } else if (ext && strcasecmp(ext, ".aac") == 0) {
        httpd_resp_set_type(req, "audio/aac");
    } else {
        httpd_resp_set_type(req, "application/octet-stream");
    }

    // Set download header
    char disposition[128];
    snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", filename);
    httpd_resp_set_hdr(req, "Content-Disposition", disposition);

    // Send file in chunks
    char *buf = malloc(4096);
    if (!buf) {
        fclose(file);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory error");
        return ESP_FAIL;
    }

    size_t read_bytes;
    while ((read_bytes = fread(buf, 1, 4096, file)) > 0) {
        if (httpd_resp_send_chunk(req, buf, read_bytes) != ESP_OK) {
            free(buf);
            fclose(file);
            return ESP_FAIL;
        }
    }

    free(buf);
    fclose(file);
    httpd_resp_send_chunk(req, NULL, 0);

    ESP_LOGI(TAG, "Downloaded: %s", filename);
    return ESP_OK;
}

// HTTP handler: Delete file
static esp_err_t delete_handler(httpd_req_t *req)
{
    char query[128];
    char filename[64];

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing filename");
        return ESP_FAIL;
    }

    if (httpd_query_key_value(query, "file", filename, sizeof(filename)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename");
        return ESP_FAIL;
    }

    char base_path[32];
    get_base_path(base_path, sizeof(base_path));
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%s", base_path, filename);

    if (unlink(full_path) != 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to delete");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Deleted: %s", filename);

    // Redirect back to index
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// HTTP handler: Upload file
static esp_err_t upload_handler(httpd_req_t *req)
{
    char *buf = malloc(4096);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory error");
        return ESP_FAIL;
    }

    // Find filename in Content-Disposition header within multipart data
    char filename[64] = "uploaded_file.wav";
    char base_path[32];
    get_base_path(base_path, sizeof(base_path));

    int total_len = req->content_len;
    int received = 0;
    int remaining = total_len;

    FILE *file = NULL;
    bool header_parsed = false;
    char *body_start = NULL;
    int header_len = 0;

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf + received, MIN(4096 - received, remaining));
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            free(buf);
            if (file) fclose(file);
            return ESP_FAIL;
        }

        received += recv_len;
        remaining -= recv_len;

        if (!header_parsed) {
            buf[received] = '\0';

            // Find filename in Content-Disposition
            char *fname_start = strstr(buf, "filename=\"");
            if (fname_start) {
                fname_start += 10;
                char *fname_end = strchr(fname_start, '"');
                if (fname_end) {
                    int fname_len = fname_end - fname_start;
                    if (fname_len > 0 && fname_len < sizeof(filename) - 1) {
                        strncpy(filename, fname_start, fname_len);
                        filename[fname_len] = '\0';
                    }
                }
            }

            // Find end of headers (double CRLF)
            body_start = strstr(buf, "\r\n\r\n");
            if (body_start) {
                body_start += 4;
                header_len = body_start - buf;
                header_parsed = true;

                // Open file for writing
                char full_path[256];
                snprintf(full_path, sizeof(full_path), "%s/%s", base_path, filename);
                file = fopen(full_path, "wb");
                if (!file) {
                    free(buf);
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file");
                    return ESP_FAIL;
                }

                // Write body data (excluding trailing boundary)
                int body_len = received - header_len;
                if (body_len > 0) {
                    fwrite(body_start, 1, body_len, file);
                }
            }
        } else if (file) {
            fwrite(buf, 1, recv_len, file);
        }

        // Reset buffer if header already parsed
        if (header_parsed) {
            received = 0;
        }
    }

    if (file) {
        // Truncate trailing boundary (approximate - remove last ~50 bytes which contain boundary)
        long pos = ftell(file);
        if (pos > 50) {
            ftruncate(fileno(file), pos - 46);
        }
        fclose(file);
        ESP_LOGI(TAG, "Uploaded: %s", filename);
    }

    free(buf);

    // Redirect back to index
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// Start HTTP server
static esp_err_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    config.max_uri_handlers = 8;

    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }

    // Register URI handlers
    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
    };
    httpd_register_uri_handler(server, &index_uri);

    httpd_uri_t download_uri = {
        .uri = "/download",
        .method = HTTP_GET,
        .handler = download_handler,
    };
    httpd_register_uri_handler(server, &download_uri);

    httpd_uri_t delete_uri = {
        .uri = "/delete",
        .method = HTTP_GET,
        .handler = delete_handler,
    };
    httpd_register_uri_handler(server, &delete_uri);

    httpd_uri_t upload_uri = {
        .uri = "/upload",
        .method = HTTP_POST,
        .handler = upload_handler,
    };
    httpd_register_uri_handler(server, &upload_uri);

    return ESP_OK;
}

// Initialize WiFi in station mode
static esp_err_t wifi_init_sta(void)
{
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to WiFi SSID: %s", WIFI_SSID);

    // Wait for connection
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(30000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to connect to WiFi");
        return ESP_FAIL;
    }
}

esp_err_t debug_server_start(void)
{
    if (is_running) {
        ESP_LOGW(TAG, "Debug server already running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting debug server...");

    // Connect to WiFi
    esp_err_t ret = wifi_init_sta();
    if (ret != ESP_OK) {
        return ret;
    }

    // Start web server
    ret = start_webserver();
    if (ret != ESP_OK) {
        esp_wifi_stop();
        return ret;
    }

    is_running = true;
    ESP_LOGI(TAG, "Debug server started - open browser to device IP");

    return ESP_OK;
}

esp_err_t debug_server_stop(void)
{
    if (!is_running) {
        return ESP_OK;
    }

    if (server) {
        httpd_stop(server);
        server = NULL;
    }

    esp_wifi_stop();
    esp_wifi_deinit();

    if (wifi_event_group) {
        vEventGroupDelete(wifi_event_group);
        wifi_event_group = NULL;
    }

    is_running = false;
    ESP_LOGI(TAG, "Debug server stopped");

    return ESP_OK;
}

bool debug_server_is_running(void)
{
    return is_running;
}
