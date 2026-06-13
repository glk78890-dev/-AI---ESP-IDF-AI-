/* WiFi station + DeepSeek AI Chatbot Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

/* WiFi configuration */
#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

#if CONFIG_ESP_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""
#elif CONFIG_ESP_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif
#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

/* DeepSeek API configuration */
#define DEEPSEEK_API_URL        "https://api.deepseek.com/chat/completions"
#define DEEPSEEK_API_KEY        "sk-d9c29e0c69654905819dcca243f66807"
#define DEEPSEEK_MODEL          CONFIG_DEEPSEEK_MODEL_STRING
#define DEEPSEEK_MAX_HISTORY    CONFIG_DEEPSEEK_MAX_HISTORY
#define DEEPSEEK_MAX_TOKENS     CONFIG_DEEPSEEK_MAX_TOKENS
#define MAX_INPUT_LEN           512
#define MAX_RESPONSE_LEN        4096
#define MAX_HISTORY_ENTRIES     (DEEPSEEK_MAX_HISTORY * 2)  /* user + assistant per round */

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "xiaozhi";

static int s_retry_num = 0;

/* -------- WiFi Event Handler -------- */
static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* -------- WiFi Init -------- */
void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or
     * connection failed for the maximum number of re-tries (WIFI_FAIL_BIT). */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

/* -------- DeepSeek API Call -------- */

/* -------- Conversation History (Ring Buffer) -------- */
typedef struct {
    char role[16];      /* "user" or "assistant" */
    char *content;      /* heap-allocated message content */
} history_entry_t;

static history_entry_t s_history[MAX_HISTORY_ENTRIES];
static int s_history_count = 0;   /* total messages ever added */
static int s_history_start = 0;   /* ring buffer start index */

/* Add a message to conversation history */
static void history_add(const char *role, const char *content)
{
    int idx;
    if (s_history_count < MAX_HISTORY_ENTRIES) {
        /* Buffer not full yet — append */
        idx = s_history_count;
        s_history_count++;
    } else {
        /* Buffer full — overwrite oldest, advance start */
        idx = s_history_start;
        free(s_history[idx].content);
        s_history_start = (s_history_start + 1) % MAX_HISTORY_ENTRIES;
    }
    strncpy(s_history[idx].role, role, sizeof(s_history[idx].role) - 1);
    s_history[idx].content = strdup(content);
}

/**
 * Build the JSON request body for DeepSeek chat completions API.
 * Includes conversation history for multi-turn context.
 * Returns a heap-allocated string that the caller must free().
 */
static char* build_request_json(const char* user_message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", DEEPSEEK_MODEL);
    cJSON_AddBoolToObject(root, "stream", false);
    cJSON_AddNumberToObject(root, "max_tokens", DEEPSEEK_MAX_TOKENS);
    cJSON_AddNumberToObject(root, "temperature", 0.7);

    /* Messages array */
    cJSON *messages = cJSON_AddArrayToObject(root, "messages");

    /* System message */
    cJSON *sys_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_msg, "role", "system");
    cJSON_AddStringToObject(sys_msg, "content", "你是小智，一个友好的AI助手。请用简洁自然的中文回答，像真人对话一样。重要：你无法获取实时信息（如当前日期、天气等），遇到此类问题请诚实告知用户你无法获取实时数据，不要编造。");
    cJSON_AddItemToArray(messages, sys_msg);

    /* Add conversation history (ring buffer, oldest first) */
    for (int i = 0; i < s_history_count && i < MAX_HISTORY_ENTRIES; i++) {
        int idx = (s_history_start + i) % MAX_HISTORY_ENTRIES;
        if (s_history[idx].content == NULL) continue;
        cJSON *hist_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(hist_msg, "role", s_history[idx].role);
        cJSON_AddStringToObject(hist_msg, "content", s_history[idx].content);
        cJSON_AddItemToArray(messages, hist_msg);
    }

    /* Current user message */
    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddStringToObject(user_msg, "content", user_message);
    cJSON_AddItemToArray(messages, user_msg);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

/**
 * HTTP event handler: collect response data into a buffer.
 */
static char response_buffer[MAX_RESPONSE_LEN];
static int response_len = 0;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_DATA:
            if (response_len + evt->data_len < MAX_RESPONSE_LEN) {
                memcpy(response_buffer + response_len, evt->data, evt->data_len);
                response_len += evt->data_len;
                response_buffer[response_len] = '\0';
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP request finished");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP disconnected");
            break;
        default:
            break;
    }
    return ESP_OK;
}

/**
 * Call DeepSeek API with the user's message.
 * Returns heap-allocated assistant response string, or NULL on error.
 * Caller must free() the returned string.
 */
static char* call_deepseek_api(const char* user_input)
{
    char *assistant_reply = NULL;
    char *request_body = NULL;

    /* Add user message to conversation history */
    history_add("user", user_input);

    /* Reset response buffer */
    memset(response_buffer, 0, sizeof(response_buffer));
    response_len = 0;

    /* Build JSON request (now includes history) */
    request_body = build_request_json(user_input);
    if (!request_body) {
        ESP_LOGE(TAG, "Failed to build request JSON");
        return NULL;
    }

    ESP_LOGI(TAG, "Sending to DeepSeek: %s", user_input);

    /* Configure HTTP client */
    esp_http_client_config_t config = {
        .url = DEEPSEEK_API_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 30000,
        .event_handler = http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    /* Set headers */
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", DEEPSEEK_API_KEY);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, request_body, strlen(request_body));

    /* Perform the request */
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP Status: %d", status_code);

        if (status_code == 200) {
            /* Parse JSON response */
            cJSON *root = cJSON_Parse(response_buffer);
            if (root) {
                cJSON *choices = cJSON_GetObjectItem(root, "choices");
                if (choices && cJSON_GetArraySize(choices) > 0) {
                    cJSON *first_choice = cJSON_GetArrayItem(choices, 0);
                    cJSON *message = cJSON_GetObjectItem(first_choice, "message");
                    cJSON *content = cJSON_GetObjectItem(message, "content");
                    if (content && content->valuestring) {
                        assistant_reply = strdup(content->valuestring);
                        /* Add assistant reply to conversation history */
                        history_add("assistant", assistant_reply);
                    }
                }

                /* Log token usage */
                cJSON *usage = cJSON_GetObjectItem(root, "usage");
                if (usage) {
                    cJSON *total_tokens = cJSON_GetObjectItem(usage, "total_tokens");
                    if (total_tokens) {
                        ESP_LOGI(TAG, "Tokens used: %d", total_tokens->valueint);
                    }
                }

                cJSON_Delete(root);
            } else {
                ESP_LOGE(TAG, "Failed to parse JSON response");
                ESP_LOGE(TAG, "Response: %s", response_buffer);
            }
        } else if (status_code == 429) {
            ESP_LOGE(TAG, "Rate limited! (429) Wait and try again.");
        } else if (status_code == 401 || status_code == 403) {
            ESP_LOGE(TAG, "Auth error! Check your API key.");
        } else {
            ESP_LOGE(TAG, "HTTP error %d: %s", status_code, response_buffer);
        }
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    }

    /* Cleanup */
    esp_http_client_cleanup(client);
    free(request_body);

    return assistant_reply;
}

/* -------- Chat Task -------- */
static void chat_task(void *pvParameters)
{
    printf("\n\n");
    printf("========================================\n");
    printf("       小智 AI 助手 (XiaoZhi)\n");
    printf("  基于 DeepSeek " DEEPSEEK_MODEL "\n");
    printf("========================================\n");
    printf("输入文字开始对话，输入 'quit' 退出\n");
    printf("----------------------------------------\n\n");

    char input_buffer[MAX_INPUT_LEN];
    int consecutive_errors = 0;
    int char_count = 0;
    int c;

    printf("你: ");
    fflush(stdout);

    while (1) {
        /* Read one character at a time (blocking) */
        c = getchar();

        /* EOF means no data available — wait and retry */
        if (c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        /* Filter out ASCII control characters only (0-31).
         * Keep everything >= 32: printable ASCII, UTF-8 multi-byte bytes (128-255),
         * plus Enter(13), Backspace(8), DEL(127). */
        if (c < 32 && c != '\n' && c != '\r' && c != '\b') {
            continue;
        }

        /* Handle backspace / DEL */
        if (c == '\b' || c == 0x7f) {
            if (char_count > 0) {
                char_count--;
                printf("\b \b");
                fflush(stdout);
            }
            continue;
        }

        /* Handle Enter — process the input line */
        if (c == '\n' || c == '\r') {
            /* If buffer is empty, just re-prompt */
            if (char_count == 0) {
                printf("\n你: ");
                fflush(stdout);
                continue;
            }

            /* Null-terminate the input */
            input_buffer[char_count] = '\0';

            /* Strip leading invalid UTF-8 bytes (continuation bytes 0x80-0xBF
             * and lone 0xC0/0xC1/0xF5-0xFF that can appear from UART noise) */
            char *clean = input_buffer;
            while (*clean) {
                unsigned char b = (unsigned char)*clean;
                if ((b >= 0x80 && b <= 0xBF) || b == 0xC0 || b == 0xC1 || b >= 0xF5) {
                    clean++;  /* skip this invalid leading byte */
                } else {
                    break;    /* valid start byte found */
                }
            }
            printf("[%s]\n", clean);

            /* If the entire input was stripped, skip */
            if (strlen(clean) == 0) {
                char_count = 0;
                printf("你: ");
                fflush(stdout);
                continue;
            }

            /* Check for quit command */
            if (strcmp(clean, "quit") == 0) {
                printf("再见！\n");
                break;
            }

            /* Call DeepSeek API */
            char *reply = call_deepseek_api(clean);

            if (reply) {
                printf("小智: %s\n\n", reply);
                consecutive_errors = 0;
                free(reply);
            } else {
                printf("小智: (抱歉，出错了，请稍后重试)\n\n");
                consecutive_errors++;
                if (consecutive_errors >= 3) {
                    printf("小智: 连续多次出错，请检查网络或 API Key 配置。\n");
                }
            }

            /* Small delay to avoid hammering the API */
            vTaskDelay(pdMS_TO_TICKS(300));

            /* Prepare for next input */
            char_count = 0;
            printf("你: ");
            fflush(stdout);
            continue;
        }

        /* Regular character — add to buffer and echo */
        if (char_count < MAX_INPUT_LEN - 1) {
            input_buffer[char_count++] = (char)c;
            putchar(c);
            fflush(stdout);
        }
    }

    ESP_LOGI(TAG, "Chat task exiting");
    vTaskDelete(NULL);
}

/* -------- Main -------- */
void app_main(void)
{
    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    /* After WiFi is connected, start the chat task */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected, starting chat task...");
        xTaskCreate(chat_task, "chat_task", 10240, NULL, 5, NULL);
    } else {
        ESP_LOGE(TAG, "WiFi connection failed, cannot start chat.");
    }
}
