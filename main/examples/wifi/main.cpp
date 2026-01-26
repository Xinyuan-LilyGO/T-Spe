/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-01-26 13:59:19
 * @LastEditTime: 2026-01-26 14:27:51
 * @License: GPL 3.0
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "cpp_bus_driver_library.h"
#include "esp_event.h"
#include "esp_wifi_remote.h"
#include <unordered_set>
#include "lilygo_device_driver_library.h"
#include "nvs_flash.h"

#define WIFI_SSID "xinyuandianzi"
#define WIFI_PASSWORD "AA15994823428"

bool wifi_scan_success_flag = false;

auto ESP32P4 = std::make_unique<Cpp_Bus_Driver::Tool>();

void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        printf("wifi sta start\n");
        // esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        printf("got ip: " IPSTR "\n", IP2STR(&event->ip_info.ip));

        printf("wifi connect success\n");
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        printf("wifi connect fail\n");
        // printf("wifi disconnected, trying to reconnect...\n");
        // esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE)
    {
        printf("wifi scan finish\n");
        uint16_t ap_count = 0;
        std::vector<wifi_ap_record_t> ap_info;

        // get scan result count
        if (esp_wifi_scan_get_ap_num(&ap_count) != ESP_OK)
        {
            printf("failed to get ap count\n");
            return;
        }

        // limit maximum count to avoid memory issues
        if (ap_count > 64)
        {
            ap_count = 64;
        }
        ap_info.resize(ap_count);

        // get wifi hotspot info
        if (esp_wifi_scan_get_ap_records(&ap_count, ap_info.data()) != ESP_OK)
        {
            printf("failed to get ap records\n");
            return;
        }

        // clear cache
        ESP_ERROR_CHECK(esp_wifi_clear_ap_list());

        printf("scan %d wifi:\n", ap_count);

        // **sort by rssi signal strength in descending order**
        std::sort(ap_info.begin(), ap_info.end(),
                  [](const wifi_ap_record_t &a, const wifi_ap_record_t &b)
                  {
                      return a.rssi > b.rssi; // higher rssi means stronger signal
                  });

        // **remove duplicate ssid**
        std::unordered_set<std::string> seen_ssids;
        std::vector<wifi_ap_record_t> unique_ap_info;

        for (const auto &info : ap_info)
        {
            std::string ssid_str(reinterpret_cast<const char *>(info.ssid)); // convert to std::string
            if (!ssid_str.empty() && seen_ssids.find(ssid_str) == seen_ssids.end())
            {
                seen_ssids.emplace(ssid_str);
                unique_ap_info.emplace_back(info);
            }
        }

        // **iterate and print wifi hotspot info**

        printf("-------------------------------------------------------------\n");
        printf("| %-32s | %4s | %7s | %17s |\n", "ssid", "rssi", "channel", "mac address");
        printf("-------------------------------------------------------------\n");

        for (const auto &info : unique_ap_info)
        {
            const char *band = (info.primary <= 14) ? "2.4ghz" : "5ghz";
            printf("| %-32s | %4d dbm | %3d (%s) | %02x:%02x:%02x:%02x:%02x:%02x |\n", info.ssid,
                   info.rssi, info.primary, band, info.bssid[0], info.bssid[1], info.bssid[2], info.bssid[3],
                   info.bssid[4], info.bssid[5]);
        }

        printf("-------------------------------------------------------------\n");

        wifi_scan_success_flag = true;
    }
    else
    {
        printf("event %s %ld\n", event_base, event_id);
    }
}

void wifi_init_sta()
{
    // initialize nvs
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        // nvs partition was truncated and needs to be erased
        // retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    while (1)
    {
        esp_wifi_scan_start(NULL, true);
        vTaskDelay(pdMS_TO_TICKS(1500));

        if (wifi_scan_success_flag == true)
        {
            wifi_scan_success_flag = false;
            printf("wifi scan success\n");
            break;
        }
    }

    wifi_config_t wifi_config =
        {
            .sta =
                {
                    .ssid = WIFI_SSID,
                    .password = WIFI_PASSWORD,
                },
        };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    esp_wifi_connect();
}

extern "C" void app_main(void)
{
    printf("Ciallo\n");

    wifi_init_sta();

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
