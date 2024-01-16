/* VT1053 driver Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <dirent.h>  // Include the dirent.h header for directory operations
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "driver/rmt.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/message_buffer.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"
#include "lwip/dns.h"
#include "lwip/err.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "nvs_flash.h"
#include "vs1053.h"

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

#define HTTP_RESUME_BIT BIT0
#define HTTP_CLOSE_BIT BIT2
#define PLAY_START_BIT BIT4

static const char* TAG = "MAIN";

static int s_retry_num = 0;

RingbufHandle_t xRingbufferConsole;
RingbufHandle_t xRingbufferBroadcast;
MessageBufferHandle_t xMessageBuffer;

#define xRingbufferConsoleSize 1024
#define xRingbufferBroadcastSize 1024
#define xMessageBufferSize 102400L

EventGroupHandle_t xEventGroup;

#define AUDIO_MISO GPIO_NUM_33
#define AUDIO_MOSI GPIO_NUM_21
#define AUDIO_CLK GPIO_NUM_26
#define AUDIO_DC GPIO_NUM_3
#define AUDIO_CS GPIO_NUM_2
#define AUDIO_RESET GPIO_NUM_4
#define DREQ GPIO_NUM_1

unsigned char* read_file(const char* filename, size_t* file_size) {
    FILE* f = fopen(filename, "rb");
    if (f == NULL) {
        ESP_LOGI(TAG, "Error could not open: %s\n", filename);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    *file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    unsigned char* file_content = (unsigned char*)malloc(*file_size);
    if (file_content == NULL) {
        ESP_LOGI(TAG, "Error could not alloc memmory\n");
        fclose(f);
        return NULL;
    }

    size_t read_size = fread(file_content, 1, *file_size, f);
    if (read_size != *file_size) {
        ESP_LOGI(TAG, "Reading Error\n");
        free(file_content);
        fclose(f);
        return NULL;
    }

    fclose(f);

    return file_content;
}

void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(1400));
    ESP_LOGI(TAG, "Initializing SPIFFS");
    vTaskDelay(pdMS_TO_TICKS(500));

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true};

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    switch(ret){
        case ESP_OK                     : ESP_LOGI(TAG,"ESP_OK                     ") ; break;
        case ESP_ERR_NO_MEM             : ESP_LOGI(TAG,"ESP_ERR_NO_MEM             "); break;
        case ESP_ERR_INVALID_STATE      : ESP_LOGI(TAG,"ESP_ERR_INVALID_STATE      "); break;
        case ESP_ERR_NOT_FOUND          : ESP_LOGI(TAG,"ESP_ERR_NOT_FOUND          "); break;
        case ESP_FAIL                   : ESP_LOGI(TAG,"ESP_FAIL                   "); break;
    }
    if (ret != ESP_OK) {
        printf("Failed to mount or format filesystem\n");
        esp_err_t ret = esp_vfs_spiffs_register(&conf);
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    VS1053_t dev;
    spi_master_init(&dev, AUDIO_CLK, AUDIO_MISO, AUDIO_MOSI, AUDIO_CS, AUDIO_DC, DREQ, AUDIO_RESET);
    // spi_master_init(&dev, AUDIO_CS, AUDIO_DC, DREQ, AUDIO_RESET);
    switchToMp3Mode(&dev);

    setVolume(&dev, 80);
    wram_write(&dev, 0x1E04, 2);  // define a velocidade de playback

    size_t file_size;
    unsigned char* buffer = read_file("/spiffs/audio/universidade.mp3", &file_size);

    ESP_LOGI(TAG, "Tamanho do arquivo MP3: %zu bytes\n", file_size);
    ESP_LOGI(TAG, "%s", buffer);

    while (1) {
        playChunk(&dev, (uint8_t*)buffer, file_size);
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}