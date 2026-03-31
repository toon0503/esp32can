/* WebSocket Echo Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "esp_netif.h"
#include "esp_eth.h"

#include <esp_http_server.h>
#include <sys/stat.h>
#include <fcntl.h>


// 添加缺失的头文件
#include <unistd.h>




/* A simple example that demonstrates using websocket echo server
 */
static const char *TAG = "ws_echo_server";

/*
 * Structure holding server handle
 * and internal socket fd in order
 * to use out of request send
 */
struct async_resp_arg {
    httpd_handle_t hd;
    int fd;
};

/*
 * async send function, which we put into the httpd work queue
 */
static void ws_async_send(void *arg)
{
    static const char * data = "Async data";
    struct async_resp_arg *resp_arg = arg;
    httpd_handle_t hd = resp_arg->hd;
    int fd = resp_arg->fd;
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t*)data;
    ws_pkt.len = strlen(data);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    httpd_ws_send_frame_async(hd, fd, &ws_pkt);
    free(resp_arg);
}

static esp_err_t trigger_async_send(httpd_handle_t handle, httpd_req_t *req)
{
    struct async_resp_arg *resp_arg = malloc(sizeof(struct async_resp_arg));
    if (resp_arg == NULL) {
        return ESP_ERR_NO_MEM;
    }
    resp_arg->hd = req->handle;
    resp_arg->fd = httpd_req_to_sockfd(req);
    esp_err_t ret = httpd_queue_work(handle, ws_async_send, resp_arg);
    if (ret != ESP_OK) {
        free(resp_arg);
    }
    return ret;
}

#ifdef CONFIG_EXAMPLE_ENABLE_WS_PRE_HANDSHAKE_CB
static esp_err_t ws_pre_handshake_cb(httpd_req_t *req)
{
    ESP_LOGI(TAG, "=== ws_pre_handshake_cb called ===");

    // Get the URI with query string
    const char *uri = req->uri;
    ESP_LOGI(TAG, "Requested URI: %s", uri ? uri : "NULL");

    // Check if the query string contains token=valid
    if (uri && strstr(uri, "token=valid") != NULL) {
        ESP_LOGI(TAG, "Valid token found, accepting handshake");
        return ESP_OK;
    } else {
        ESP_LOGI(TAG, "No valid token found, rejecting handshake");
        return ESP_FAIL;
    }
}
#endif

/*
 * This handler echos back the received ws data
 * and triggers an async send if certain message received
 */
static esp_err_t echo_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        return ESP_OK;
    }
    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    /* Set max_len = 0 to get the frame len */
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }
    ESP_LOGI(TAG, "frame len is %d", ws_pkt.len);
    if (ws_pkt.len) {
        /* ws_pkt.len + 1 is for NULL termination as we are expecting a string */
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL) {
            ESP_LOGE(TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        /* Set max_len = ws_pkt.len to get the frame payload */
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }
        ESP_LOGI(TAG, "Got packet with message: %s", ws_pkt.payload);
    }
    ESP_LOGI(TAG, "Packet type: %d", ws_pkt.type);
    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT &&
        ws_pkt.payload != NULL &&
        strcmp((char*)ws_pkt.payload,"Trigger async") == 0) {
        free(buf);
        return trigger_async_send(req->handle, req);
    }

    ret = httpd_ws_send_frame(req, &ws_pkt);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_send_frame failed with %d", ret);
    }
    free(buf);
    return ret;
}

static const httpd_uri_t ws = {
        .uri        = "/ws",
        .method     = HTTP_GET,
        .handler    = echo_handler,
        .user_ctx   = NULL,
        .is_websocket = true
};

static const httpd_uri_t ws_auth = {
        .uri        = "/auth",
        .method     = HTTP_GET,
        .handler    = echo_handler,
        .user_ctx   = NULL,
        .is_websocket = true,
#ifdef CONFIG_EXAMPLE_ENABLE_WS_PRE_HANDSHAKE_CB
        .ws_pre_handshake_cb = ws_pre_handshake_cb
#endif
};

// 修改index_handler函数，使用正确的文件发送方法
// 重写index_handler函数
static esp_err_t index_handler(httpd_req_t *req) {
    extern const uint8_t index_html_start[] asm("_binary_index_html_start");
    extern const uint8_t index_html_end[] asm("_binary_index_html_end");
    size_t index_html_size = index_html_end - index_html_start;
    
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)index_html_start, index_html_size);
    
    return ESP_OK;
}

// 删除未使用的stop_webserver函数
static const httpd_uri_t index_uri = {
        .uri        = "/",
        .method     = HTTP_GET,
        .handler    = index_handler,
        .user_ctx   = NULL
};

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Registering the ws handler
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &ws);
        httpd_register_uri_handler(server, &ws_auth);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

static esp_err_t stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    return httpd_stop(server);
}

static void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_webserver();
    }
}


void app_main(void)
{
    static httpd_handle_t server = NULL;

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Configure Wi-Fi in AP mode */
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    assert(ap_netif != NULL);
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Configure AP settings */
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "ESP32-AP",
            .ssid_len = strlen("ESP32-AP"),
            .password = "12345678",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP started with SSID: ESP32-AP, password: 12345678");

    /* Register event handlers */
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &connect_handler, &server));

    /* Start the server for the first time */
    server = start_webserver();
}
