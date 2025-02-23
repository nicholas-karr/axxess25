#include <esp_http_server.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_tls.h>
#include <esp_crt_bundle.h>

#include <ArduinoJson.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define ABS(a) fabsf(a)
#define TAG "http"

void op(int shift, const char* text);
void loadConfig();
void loadCalendar();

esp_err_t getHandler(httpd_req_t *req)
{
    const char resp[] = "URI GET Response";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

    printf("Req to URI %s\n", req->uri);

    return ESP_OK;
}

esp_err_t reloadHandler(httpd_req_t *req)
{
    const char resp[] = "Reloaded!";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

    printf("Req to URI %s\n", req->uri);

    loadConfig();

    return ESP_OK;
}

esp_err_t postHandler(httpd_req_t *req)
{
    char content[100];

    size_t recv_size = MIN(req->content_len, sizeof(content));

    int ret = httpd_req_recv(req, content, recv_size);
    if (ret <= 0) {  /* 0 return value indicates connection closed */
        /* Check if timeout occurred */
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            /* In case of timeout one can choose to retry calling
             * httpd_req_recv(), but to keep it simple, here we
             * respond with an HTTP 408 (Request Timeout) error */
            httpd_resp_send_408(req);
        }
        /* In case of error, returning ESP_FAIL will
         * ensure that the underlying socket is closed */
        return ESP_FAIL;
    }

    printf("Got POST %s\n", content);

    JsonDocument json;

    // Read JSON packet
    DeserializationError error = deserializeJson(json, content);

    if (error) {
        ESP_LOGE("", "deserializeJson() failed: %s", error.c_str());
        return ESP_OK;
    }

    auto shift = json["shift"].as<int32_t>();
    const char* text = json["text"].as<const char*>();
    op(shift, text);

    const char resp[] = "URI POST Response";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* URI handler structure for GET /uri */
httpd_uri_t uri_get = {
    .uri      = "/api",
    .method   = HTTP_GET,
    .handler  = getHandler,
    .user_ctx = NULL
};

/* URI handler structure for GET /uri */
httpd_uri_t uri_reload = {
    .uri      = "/reload",
    .method   = HTTP_GET,
    .handler  = reloadHandler,
    .user_ctx = NULL
};

/* URI handler structure for POST /uri */
httpd_uri_t uri_post = {
    .uri      = "/api",
    .method   = HTTP_POST,
    .handler  = postHandler,
    .user_ctx = NULL
};

/* Function for starting the webserver */
httpd_handle_t start_webserver(void)
{
    /* Generate default configuration */
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    /* Empty handle to esp_http_server */
    httpd_handle_t server = NULL;

    /* Start the httpd server */
    if (httpd_start(&server, &config) == ESP_OK) {
        /* Register URI handlers */
        httpd_register_uri_handler(server, &uri_get);
        httpd_register_uri_handler(server, &uri_reload);
        httpd_register_uri_handler(server, &uri_post);
    }
    /* If server failed to start, handle will be NULL */
    return server;
}

/* Function for stopping the webserver */
void stop_webserver(httpd_handle_t server)
{
    if (server) {
        /* Stop the httpd server */
        httpd_stop(server);
    }
}

#define HTTP_RESP_SIZE 30000
char httpResp[HTTP_RESP_SIZE];

esp_err_t httpEventHandler(esp_http_client_event_t* evt)
{
    static int output_len = 0;
    static char* output_buffer = nullptr;

    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA: {
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);

            // Clean the buffer in case of a new request
            if (output_len == 0) {
                // we are just starting to copy the output data into the use
                memset(httpResp, 0, HTTP_RESP_SIZE);
                output_buffer = httpResp;
            }
            /*
             *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
             *  However, event handler can also be used in case chunked encoding is used.
             */
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // If user_data buffer is configured, copy the response into the buffer
                int copy_len = 0;
                // The last byte in httpResp is kept for the NULL character in case of out-of-bound access.
                copy_len = MIN(evt->data_len, (HTTP_RESP_SIZE - output_len));
                if (copy_len) {
                    memcpy(httpResp + output_len, evt->data, copy_len);
                }
                output_len += copy_len;
            }

        break;
    }
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        output_len = 0;
        break;
    case HTTP_EVENT_DISCONNECTED: {
        ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
        int mbedtls_err = 0;
        esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
        if (err != 0) {
            ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
            ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
        }

        break;
    }
    case HTTP_EVENT_REDIRECT:
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
        esp_http_client_set_redirection(evt->client);
        break;
    }
    return ESP_OK;
}

esp_err_t getJsonFromPath(const char* host, const char* path, JsonDocument& doc)
{
    esp_http_client_config_t httpConfig = {};
    httpConfig.host = host;
    httpConfig.path = path;
    httpConfig.transport_type = HTTP_TRANSPORT_OVER_SSL;
    httpConfig.event_handler = httpEventHandler;
    httpConfig.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&httpConfig);

    memset(httpResp, 0, sizeof(httpResp));
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Successful HTTP request to %s, status %d, length %d", host, esp_http_client_get_status_code(client), (int)esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "Failed HTTP request to %s", host);
        esp_http_client_cleanup(client);
        return err;
    }

    ESP_LOGI(TAG, "Parsing JSON %s", httpResp);
    DeserializationError jerr = deserializeJson(doc, httpResp);

    if (jerr) {
        ESP_LOGE(TAG, "Failed deserializing json from %s because %s", host, "");
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    esp_http_client_cleanup(client);
    return ESP_OK;
}

char* getFileFromPath(const char* host, const char* path) {
    esp_http_client_config_t httpConfig = {};
    httpConfig.host = host;
    httpConfig.path = path;
    httpConfig.transport_type = HTTP_TRANSPORT_OVER_SSL;
    httpConfig.event_handler = httpEventHandler;
    httpConfig.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&httpConfig);

    memset(httpResp, 0, sizeof(httpResp));
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Successful HTTP request to %s, status %d, length %d", host, esp_http_client_get_status_code(client), (int)esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "Failed HTTP request to %s", host);
        esp_http_client_cleanup(client);
        return nullptr;
    }

    esp_http_client_cleanup(client);
    return httpResp;
}

#undef TAG