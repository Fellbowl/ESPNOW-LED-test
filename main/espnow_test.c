#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "driver/gpio.h"

// ---------------------------------------------------------
// CONFIGURATION
// Change this to 0 for the Receiver ESP32, and 1 for the Sender
#define IS_SENDER 0
// ---------------------------------------------------------

#define BLINK_GPIO 2 // Default integrated LED on most ESP32 boards

static const char *TAG = "ESPNOW_TEST";

// Broadcast MAC address
static uint8_t broadcast_mac[ESP_NOW_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// Data structure to send over ESPNOW
typedef struct {
    bool led_state;
} espnow_command_t;


// Send callback
static void espnow_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    ESP_LOGI(TAG, "Last Packet Send Status: %s", status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

// Receive callback
static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    if (len == sizeof(espnow_command_t)) {
        espnow_command_t *cmd = (espnow_command_t *)data;
        ESP_LOGI(TAG, "Received command: Turn LED %s", cmd->led_state ? "ON" : "OFF");
        
        // Turn the integrated LED ON or OFF based on the received command
        gpio_set_level(BLINK_GPIO, cmd->led_state);
    } else {
        ESP_LOGW(TAG, "Received unknown data size: %d bytes", len);
    }
}

// Wi-Fi initialization
static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_start() );
}

// ESPNOW initialization
static void espnow_init(void)
{
    ESP_ERROR_CHECK( esp_now_init() );
    ESP_ERROR_CHECK( esp_now_register_send_cb(espnow_send_cb) );
    ESP_ERROR_CHECK( esp_now_register_recv_cb(espnow_recv_cb) );

    // Add broadcast peer
    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, broadcast_mac, ESP_NOW_ETH_ALEN);
    peer_info.channel = 0; // Use current channel
    peer_info.ifidx = WIFI_IF_STA;
    peer_info.encrypt = false;
    
    if (esp_now_add_peer(&peer_info) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add broadcast peer");
        return;
    }
    ESP_LOGI(TAG, "Broadcast peer added successfully");
}

// Task to send messages periodically (Only used by SENDER)
static void send_task(void *pvParameter)
{
    espnow_command_t cmd;
    cmd.led_state = false;

    while (1) {
        // Toggle the state
        cmd.led_state = !cmd.led_state; 
        
        ESP_LOGI(TAG, "Sending command: Turn LED %s", cmd.led_state ? "ON" : "OFF");
        esp_err_t result = esp_now_send(broadcast_mac, (uint8_t *)&cmd, sizeof(espnow_command_t));
        
        if (result == ESP_OK) {
            // Optional: You could also toggle the sender's own LED to see what it's transmitting
            gpio_set_level(BLINK_GPIO, cmd.led_state); 
        } else {
            ESP_LOGE(TAG, "Error sending command: %d", result);
        }

        vTaskDelay(pdMS_TO_TICKS(2000)); // Delay for 2 seconds
    }
}

void app_main(void)
{
    // Initialize NVS (required for Wi-Fi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    // Initialize the GPIO for the integrated LED
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(BLINK_GPIO, 0); // Start with LED off

    // Initialize Wi-Fi
    wifi_init();

    // Initialize ESPNOW
    espnow_init();

    // If this board is configured as the sender, start the sending task
    if (IS_SENDER) {
        ESP_LOGI(TAG, "Starting as SENDER. Toggling LED command every 2 seconds...");
        xTaskCreate(send_task, "espnow_send_task", 4096, NULL, 4, NULL);
    } else {
        ESP_LOGI(TAG, "Starting as RECEIVER. Waiting for LED commands...");
    }
}
