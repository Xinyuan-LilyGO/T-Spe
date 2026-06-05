/*
 * @Description: general_test
 * @Author: LILYGO_L
 * @Date: 2026-01-29 17:59:59
 * @LastEditTime: 2026-06-04 11:54:38
 * @License: GPL 3.0
 */
#include "lilygo_device_driver_library.h"
#include "cpp_bus_driver_library.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "lwip/sockets.h"
#include "ethernet_init.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "esp_sntp.h"
#include "esp_err.h"
#include "sdkconfig.h"
#if CONFIG_ETHERNET_USE_PLCA
#include "esp_eth_phy_lan86xx.h"
#endif
#include <vector>
#include <string>
#include <sstream>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define SOFTWARE_NAME "general_test"
#define BOARD_VERSION "v1.0"

#define WIFI_SSID "LilyGo-AABB"
#define WIFI_PASSWORD "xinyuandianzi"

#define WIFI_TIME_SYNC_TIMEOUT_MS 15000
#define WIFI_TIMEZONE "CST-8"
#define WIFI_NTP_SERVER_0 "ntp.aliyun.com"

#define ETH_MAX_TRANSMIT_SIZE 1400
#define ETH_SERVER_PORT 5001
#define ETH_CLIENT_PORT 5002
#define ETH_TEST_HEADER_SIZE 16
#define ETH_ERROR_SNIPPET_BYTES 24
#define ETH_MISMATCH_PRINT_LIMIT 8
#define ETH_IGNORED_ENDPOINT_PRINT_LIMIT 3
#define ETH_IGNORED_ENDPOINT_PRINT_INTERVAL 16
#define ETH_TASK_LOOP_DELAY_MS 2
#define ETH_SEND_BACKOFF_DELAY_MS 20
#define ETH_START_WAIT_MS 2000
#define ETH_STOP_WAIT_MS 100
#define RS485_MAX_TRANSMIT_SIZE 1024

bool Wifi_Connect_Flag = false;
bool All_Exit_Test_Flag = false;
bool Wifi_Time_Sntp_Start_Flag = false;
volatile bool Wifi_Time_Sync_Flag = false;

TaskHandle_t Wifi_Download_Test_Task_Handle = nullptr;
TaskHandle_t Eth_Test_Task_Handle = nullptr;
TaskHandle_t Rs485_Test_Task_Handle = nullptr;

auto Uart_Bus = std::make_shared<Cpp_Bus_Driver::Hardware_Uart>(TD301D485H_A_TX, TD301D485H_A_RX, -1, -1, UART_NUM_1);
auto ESP32 = std::make_unique<Cpp_Bus_Driver::Tool>();

struct EthPacketStats
{
    size_t packet_count = 0;
    size_t bad_packet_count = 0;
    size_t ignored_packet_count = 0;
    size_t source_error_count = 0;
    size_t length_error_count = 0;
    size_t magic_error_count = 0;
    size_t sequence_error_count = 0;
    size_t payload_error_count = 0;
    size_t checksum_error_count = 0;
    bool sequence_valid = false;
    uint32_t expected_sequence = 0;
};

static int get_build_month(const char *date)
{
    static constexpr char month_names[][4] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };

    for (int i = 0; i < 12; i++)
    {
        if (date[0] == month_names[i][0] && date[1] == month_names[i][1] && date[2] == month_names[i][2])
        {
            return i + 1;
        }
    }

    return 0;
}

static std::string get_software_build_time()
{
    const esp_app_desc_t *app_desc = esp_app_get_description();
    const char *date = app_desc->date;
    const char *time = app_desc->time;
    int month = get_build_month(date);

    char build_time[13] = {
        date[7],
        date[8],
        date[9],
        date[10],
        static_cast<char>('0' + month / 10),
        static_cast<char>('0' + month % 10),
        date[4] == ' ' ? '0' : date[4],
        date[5],
        time[0],
        time[1],
        time[3],
        time[4],
        '\0',
    };

    return std::string(build_time);
}

static void print_wifi_time()
{
    time_t now = 0;
    struct tm timeinfo = {};
    char time_buf[64] = {};

    time(&now);

    setenv("TZ", WIFI_TIMEZONE, 1);
    tzset();
    localtime_r(&now, &timeinfo);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    printf("[wifi] local time: %s\n", time_buf);

    gmtime_r(&now, &timeinfo);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    printf("[wifi] utc time: %s\n", time_buf);
}

static void wifi_time_sync_notification_cb(struct timeval *tv)
{
    (void)tv;
    Wifi_Time_Sync_Flag = true;
}

bool validate_data(const char *tag, const char *data, size_t len, char expected_char)
{
    for (size_t i = 0; i < len; i++)
    {
        if (data[i] != expected_char)
        {
            printf("\n[%s] !!! data corruption error !!!\n", tag);
            printf("[%s] offset: %zu, expected: %#X ('%c'), got: %#X ('%c')\n",
                   tag, i, expected_char, expected_char, (unsigned char)data[i], data[i]);

            // 打印错误点前后的一小段数据供分析
            printf("[%s] data snippet: ", tag);
            size_t start = (i > 5) ? i - 5 : 0;
            for (size_t j = start; j < (i + 5 > len ? len : i + 5); j++)
            {
                printf("%#X ", (unsigned char)data[j]);
            }
            printf("\n");

            All_Exit_Test_Flag = true; // 触发全局停止
            return false;
        }
    }
    return true;
}

static void write_u16_le(char *data, uint16_t value)
{
    data[0] = static_cast<char>(value & 0xFF);
    data[1] = static_cast<char>((value >> 8) & 0xFF);
}

static uint16_t read_u16_le(const char *data)
{
    return static_cast<uint16_t>(static_cast<uint8_t>(data[0])) |
           (static_cast<uint16_t>(static_cast<uint8_t>(data[1])) << 8);
}

static void write_u32_le(char *data, uint32_t value)
{
    data[0] = static_cast<char>(value & 0xFF);
    data[1] = static_cast<char>((value >> 8) & 0xFF);
    data[2] = static_cast<char>((value >> 16) & 0xFF);
    data[3] = static_cast<char>((value >> 24) & 0xFF);
}

static uint32_t read_u32_le(const char *data)
{
    return static_cast<uint32_t>(static_cast<uint8_t>(data[0])) |
           (static_cast<uint32_t>(static_cast<uint8_t>(data[1])) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(data[2])) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(data[3])) << 24);
}

static uint8_t eth_expected_payload_byte(uint32_t sequence, size_t payload_offset)
{
    return static_cast<uint8_t>('A' + ((sequence + payload_offset) % 26));
}

static uint32_t eth_calc_checksum(const char *data, size_t len)
{
    uint32_t checksum = 2166136261u;
    for (size_t i = 0; i < len; i++)
    {
        checksum ^= static_cast<uint8_t>(data[i]);
        checksum *= 16777619u;
    }

    return checksum;
}

static void eth_build_test_packet(char *data, size_t len, uint32_t sequence)
{
    if (len < ETH_TEST_HEADER_SIZE)
    {
        return;
    }

    size_t payload_len = len - ETH_TEST_HEADER_SIZE;

    data[0] = 'T';
    data[1] = 'S';
    data[2] = 'P';
    data[3] = 'E';
    write_u32_le(&data[4], sequence);
    write_u16_le(&data[8], static_cast<uint16_t>(payload_len));
    write_u16_le(&data[10], ETH_TEST_HEADER_SIZE);

    char *payload = data + ETH_TEST_HEADER_SIZE;
    for (size_t i = 0; i < payload_len; i++)
    {
        payload[i] = static_cast<char>(eth_expected_payload_byte(sequence, i));
    }

    write_u32_le(&data[12], eth_calc_checksum(payload, payload_len));
}

static void eth_format_ipv4(uint32_t network_addr, char *buffer, size_t buffer_len)
{
    uint32_t host_addr = ntohl(network_addr);
    snprintf(buffer, buffer_len, "%lu.%lu.%lu.%lu",
             static_cast<unsigned long>((host_addr >> 24) & 0xFF),
             static_cast<unsigned long>((host_addr >> 16) & 0xFF),
             static_cast<unsigned long>((host_addr >> 8) & 0xFF),
             static_cast<unsigned long>(host_addr & 0xFF));
}

static bool eth_is_expected_client_endpoint(const struct sockaddr_in *source_addr)
{
    uint32_t host_addr = ntohl(source_addr->sin_addr.s_addr);
    uint32_t expected_addr = (192UL << 24) | (168UL << 16) | (0UL << 8) | 2UL;
    return host_addr == expected_addr && ntohs(source_addr->sin_port) == ETH_CLIENT_PORT;
}

static void eth_print_hex_bytes(const char *tag, const char *label, const char *data, size_t len, size_t max_len)
{
    size_t print_len = (len < max_len) ? len : max_len;

    printf("[%s] %s:", tag, label);
    for (size_t i = 0; i < print_len; i++)
    {
        printf(" %#X", static_cast<unsigned int>(static_cast<uint8_t>(data[i])));
    }
    if (len > print_len)
    {
        printf(" ...");
    }
    printf("\n");
}

static void eth_print_packet_context(const char *reason, const char *data, size_t len,
                                     const struct sockaddr_in *source_addr, const EthPacketStats *stats)
{
    char source_ip[16] = {};
    eth_format_ipv4(source_addr->sin_addr.s_addr, source_ip, sizeof(source_ip));

    printf("\n[eth server] !!! packet check fail !!!\n");
    printf("[eth server] reason: %s\n", reason);
    printf("[eth server] packet count: %zu | bad packets: %zu\n",
           stats->packet_count, stats->bad_packet_count);
    printf("[eth server] source: %s:%u | len: %zu bytes | expected len: %u bytes\n",
           source_ip, static_cast<unsigned int>(ntohs(source_addr->sin_port)), len,
           static_cast<unsigned int>(ETH_MAX_TRANSMIT_SIZE));

    if (len > 0)
    {
        eth_print_hex_bytes("eth server", "first bytes", data, len, ETH_ERROR_SNIPPET_BYTES);
        if (len > ETH_ERROR_SNIPPET_BYTES)
        {
            size_t tail_len = (len < ETH_ERROR_SNIPPET_BYTES) ? len : ETH_ERROR_SNIPPET_BYTES;
            eth_print_hex_bytes("eth server", "last bytes", data + len - tail_len, tail_len, tail_len);
        }
    }
}

static bool eth_validate_test_packet(const char *data, size_t len,
                                     const struct sockaddr_in *source_addr, EthPacketStats *stats)
{
    stats->packet_count++;

    if (eth_is_expected_client_endpoint(source_addr) == false)
    {
        stats->ignored_packet_count++;
        stats->source_error_count++;
        if (stats->source_error_count <= ETH_IGNORED_ENDPOINT_PRINT_LIMIT ||
            (stats->source_error_count % ETH_IGNORED_ENDPOINT_PRINT_INTERVAL) == 0)
        {
            char source_ip[16] = {};
            eth_format_ipv4(source_addr->sin_addr.s_addr, source_ip, sizeof(source_ip));

            printf("\n[eth server] --- ignored packet ---\n");
            printf("[eth server] reason: unexpected source endpoint\n");
            printf("[eth server] source: %s:%u | len: %zu bytes | expected source: 192.168.0.2:%u\n",
                   source_ip, static_cast<unsigned int>(ntohs(source_addr->sin_port)), len,
                   static_cast<unsigned int>(ETH_CLIENT_PORT));
            printf("[eth server] packet count: %zu | ignored packets: %zu\n",
                   stats->packet_count, stats->ignored_packet_count);
            if (len > 0)
            {
                eth_print_hex_bytes("eth server", "ignored first bytes", data, len, ETH_ERROR_SNIPPET_BYTES);
            }
        }
        return false;
    }

    if (len != ETH_MAX_TRANSMIT_SIZE)
    {
        stats->bad_packet_count++;
        stats->length_error_count++;
        eth_print_packet_context("unexpected packet length", data, len, source_addr, stats);
        All_Exit_Test_Flag = true;
        return false;
    }

    if (len < ETH_TEST_HEADER_SIZE)
    {
        stats->bad_packet_count++;
        stats->length_error_count++;
        eth_print_packet_context("packet shorter than test header", data, len, source_addr, stats);
        All_Exit_Test_Flag = true;
        return false;
    }

    if (data[0] != 'T' || data[1] != 'S' || data[2] != 'P' || data[3] != 'E')
    {
        stats->bad_packet_count++;
        stats->magic_error_count++;
        eth_print_packet_context("bad packet magic", data, len, source_addr, stats);
        printf("[eth server] magic got: %#X %#X %#X %#X | expected: 0X54 0X53 0X50 0X45\n",
               static_cast<unsigned int>(static_cast<uint8_t>(data[0])),
               static_cast<unsigned int>(static_cast<uint8_t>(data[1])),
               static_cast<unsigned int>(static_cast<uint8_t>(data[2])),
               static_cast<unsigned int>(static_cast<uint8_t>(data[3])));
        All_Exit_Test_Flag = true;
        return false;
    }

    uint32_t sequence = read_u32_le(&data[4]);
    uint16_t payload_len = read_u16_le(&data[8]);
    uint16_t header_len = read_u16_le(&data[10]);
    uint32_t received_checksum = read_u32_le(&data[12]);
    size_t expected_payload_len = len - ETH_TEST_HEADER_SIZE;
    const char *payload = data + ETH_TEST_HEADER_SIZE;
    uint32_t calculated_checksum = eth_calc_checksum(payload, expected_payload_len);
    bool ok = true;

    if (payload_len != expected_payload_len || header_len != ETH_TEST_HEADER_SIZE)
    {
        stats->length_error_count++;
        ok = false;
    }

    if (stats->sequence_valid == false)
    {
        stats->sequence_valid = true;
        stats->expected_sequence = sequence;
    }
    if (sequence != stats->expected_sequence)
    {
        stats->sequence_error_count++;
        ok = false;
    }

    size_t mismatch_count = 0;
    size_t mismatch_offsets[ETH_MISMATCH_PRINT_LIMIT] = {};
    uint8_t mismatch_expected[ETH_MISMATCH_PRINT_LIMIT] = {};
    uint8_t mismatch_actual[ETH_MISMATCH_PRINT_LIMIT] = {};

    for (size_t i = 0; i < expected_payload_len; i++)
    {
        uint8_t expected_byte = eth_expected_payload_byte(sequence, i);
        uint8_t actual_byte = static_cast<uint8_t>(payload[i]);

        if (actual_byte != expected_byte)
        {
            if (mismatch_count < ETH_MISMATCH_PRINT_LIMIT)
            {
                mismatch_offsets[mismatch_count] = ETH_TEST_HEADER_SIZE + i;
                mismatch_expected[mismatch_count] = expected_byte;
                mismatch_actual[mismatch_count] = actual_byte;
            }
            mismatch_count++;
        }
    }

    if (mismatch_count > 0)
    {
        stats->payload_error_count++;
        ok = false;
    }

    if (received_checksum != calculated_checksum)
    {
        stats->checksum_error_count++;
        ok = false;
    }

    if (ok == false)
    {
        stats->bad_packet_count++;
        eth_print_packet_context("packet content error", data, len, source_addr, stats);
        printf("[eth server] sequence: %lu | expected sequence: %lu\n",
               static_cast<unsigned long>(sequence), static_cast<unsigned long>(stats->expected_sequence));
        printf("[eth server] header len: %u | payload len: %u | expected payload len: %u\n",
               static_cast<unsigned int>(header_len), static_cast<unsigned int>(payload_len),
               static_cast<unsigned int>(expected_payload_len));
        printf("[eth server] checksum recv: 0X%08lX | calc: 0X%08lX\n",
               static_cast<unsigned long>(received_checksum), static_cast<unsigned long>(calculated_checksum));
        printf("[eth server] error counters: source=%zu, length=%zu, magic=%zu, sequence=%zu, payload=%zu, checksum=%zu\n",
               stats->source_error_count, stats->length_error_count, stats->magic_error_count,
               stats->sequence_error_count, stats->payload_error_count, stats->checksum_error_count);

        if (mismatch_count > 0)
        {
            size_t print_count = (mismatch_count < ETH_MISMATCH_PRINT_LIMIT) ? mismatch_count : ETH_MISMATCH_PRINT_LIMIT;
            printf("[eth server] payload mismatches: %zu / %u bytes\n",
                   mismatch_count, static_cast<unsigned int>(expected_payload_len));
            for (size_t i = 0; i < print_count; i++)
            {
                printf("[eth server] mismatch[%zu]: offset=%zu expected=%#X got=%#X\n",
                       i, mismatch_offsets[i], static_cast<unsigned int>(mismatch_expected[i]),
                       static_cast<unsigned int>(mismatch_actual[i]));
            }
        }

        All_Exit_Test_Flag = true;
        return false;
    }

    stats->expected_sequence = sequence + 1;
    return true;
}

#if CONFIG_ETHERNET_USE_PLCA
static bool eth_plca_ioctl(esp_eth_handle_t eth_handle, esp_eth_io_cmd_t cmd, void *value, const char *name)
{
    esp_err_t err = esp_eth_ioctl(eth_handle, cmd, value);
    if (err != ESP_OK)
    {
        printf("[eth] PLCA %s fail: %s\n", name, esp_err_to_name(err));
        return false;
    }

    return true;
}

static bool configure_eth_plca(esp_eth_handle_t eth_handle, bool is_server)
{
    bool enable = false;
    bool ok = true;
    uint8_t node_count = 2;
    uint8_t plca_id = is_server ? 0 : 1;
    uint8_t burst_count = 0;
    uint8_t transmit_opportunity_timer = 32;

    ok = eth_plca_ioctl(eth_handle, (esp_eth_io_cmd_t)LAN86XX_ETH_CMD_S_EN_PLCA, &enable, "disable") && ok;
    ok = eth_plca_ioctl(eth_handle, (esp_eth_io_cmd_t)LAN86XX_ETH_CMD_S_PLCA_NCNT, &node_count, "set node count") && ok;
    ok = eth_plca_ioctl(eth_handle, (esp_eth_io_cmd_t)LAN86XX_ETH_CMD_S_PLCA_ID, &plca_id, "set id") && ok;
    ok = eth_plca_ioctl(eth_handle, (esp_eth_io_cmd_t)LAN86XX_ETH_CMD_S_MAX_BURST_COUNT, &burst_count, "set burst count") && ok;
    ok = eth_plca_ioctl(eth_handle, (esp_eth_io_cmd_t)LAN86XX_ETH_CMD_S_PLCA_TOT, &transmit_opportunity_timer, "set TOT") && ok;

    enable = true;
    ok = eth_plca_ioctl(eth_handle, (esp_eth_io_cmd_t)LAN86XX_ETH_CMD_S_EN_PLCA, &enable, "enable") && ok;
    if (ok == false)
    {
        return false;
    }

    printf("[eth] PLCA enabled, role: %s, id: %u, node count: %u\n",
           is_server ? "coordinator" : "follower", plca_id, node_count);
    return true;
}
#endif

void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        Wifi_Connect_Flag = false;
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        Wifi_Connect_Flag = true;
        printf("[wifi] status: connected\n");
    }
}

void wifi_time_test_task(void *pv)
{
    printf("wifi_time_test_task start\n");

    printf("[wifi] preparing wifi time sync...\n");
    Wifi_Connect_Flag = false;
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_wifi_connect();
    // 等待WiFi重新连接
    for (int i = 0; i < 10; i++)
    {
        if (Wifi_Connect_Flag == true)
        {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    if (Wifi_Connect_Flag == false)
    {
        printf("wifi_time_test_task fail: wifi not connected\n");

        printf("[wifi] task completed\n");
        Wifi_Download_Test_Task_Handle = nullptr;
        vTaskDelete(NULL);
        return;
    }


    printf("[wifi] syncing time by SNTP...\n");
    printf("[wifi] ntp server: %s\n", WIFI_NTP_SERVER_0);
    Wifi_Time_Sync_Flag = false;
    if (Wifi_Time_Sntp_Start_Flag == true)
    {
        esp_sntp_stop();
        Wifi_Time_Sntp_Start_Flag = false;
    }

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, WIFI_NTP_SERVER_0);
    esp_sntp_set_time_sync_notification_cb(wifi_time_sync_notification_cb);
    esp_sntp_init();
    Wifi_Time_Sntp_Start_Flag = true;

    int64_t sync_start_time = esp_timer_get_time();
    while (Wifi_Time_Sync_Flag == false && (esp_timer_get_time() - sync_start_time) < (WIFI_TIME_SYNC_TIMEOUT_MS * 1000))
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (Wifi_Time_Sync_Flag == true)
    {
        print_wifi_time();
    }
    else
    {
        printf("[wifi] time sync timeout\n");
    }

    esp_sntp_stop();
    Wifi_Time_Sntp_Start_Flag = false;

    printf("[wifi] task completed\n");
    Wifi_Download_Test_Task_Handle = nullptr;
    vTaskDelete(NULL);
}

void eth_test_task(void *pv)
{
    bool is_server = (bool)pv;
    printf("eth_test_task (%s) start\n", is_server ? "server" : "client");

    int sock = -1;
    bool eth_started = false;
    esp_eth_handle_t *eth_handle = nullptr;
    esp_eth_netif_glue_handle_t eth_netif_glues[1] = {};

    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&cfg);
    if (eth_netif == nullptr)
    {
        printf("[eth] esp_netif_new fail\n");
        printf("[eth] task completed\n");
        Eth_Test_Task_Handle = nullptr;
        vTaskDelete(NULL);
        return;
    }

    auto cleanup_eth = [&]() {
        if (sock >= 0)
        {
            close(sock);
        }
        if (eth_started == true && eth_handle != nullptr)
        {
            esp_eth_stop(eth_handle[0]);
            vTaskDelay(pdMS_TO_TICKS(ETH_STOP_WAIT_MS));
        }
        if (eth_netif_glues[0] != nullptr)
        {
            esp_eth_del_netif_glue(eth_netif_glues[0]);
        }
        if (eth_netif != nullptr)
        {
            esp_netif_destroy(eth_netif);
        }
        if (eth_handle != nullptr)
        {
            ethernet_deinit_all(eth_handle);
        }

        printf("[eth] task completed\n");
        Eth_Test_Task_Handle = nullptr;
        vTaskDelete(NULL);
    };

    esp_netif_dhcpc_stop(eth_netif);

    esp_netif_ip_info_t ip_info;
    ip_info.ip.addr = ESP_IP4TOADDR(192, 168, 0, (is_server ? 1 : 2));
    ip_info.netmask.addr = ESP_IP4TOADDR(255, 255, 255, 0);
    ip_info.gw.addr = ESP_IP4TOADDR(0, 0, 0, 0);
    esp_err_t err = esp_netif_set_ip_info(eth_netif, &ip_info);
    if (err != ESP_OK)
    {
        printf("[eth] esp_netif_set_ip_info fail: %s\n", esp_err_to_name(err));
        cleanup_eth();
        return;
    }

    uint8_t eth_port = 0;
    err = ethernet_init_all(&eth_handle, &eth_port);
    if (err != ESP_OK || eth_handle == nullptr || eth_port == 0)
    {
        printf("[eth] ethernet_init_all fail: %s, port count: %u\n", esp_err_to_name(err), eth_port);
        cleanup_eth();
        return;
    }
#if CONFIG_ETHERNET_USE_PLCA
    if (configure_eth_plca(eth_handle[0], is_server) == false)
    {
        cleanup_eth();
        return;
    }
#endif
    eth_netif_glues[0] = esp_eth_new_netif_glue(eth_handle[0]);
    if (eth_netif_glues[0] == nullptr)
    {
        printf("[eth] esp_eth_new_netif_glue fail\n");
        cleanup_eth();
        return;
    }

    err = esp_netif_attach(eth_netif, eth_netif_glues[0]);
    if (err != ESP_OK)
    {
        printf("[eth] esp_netif_attach fail: %s\n", esp_err_to_name(err));
        cleanup_eth();
        return;
    }

    err = esp_eth_start(eth_handle[0]);
    if (err != ESP_OK)
    {
        printf("[eth] esp_eth_start fail: %s\n", esp_err_to_name(err));
        cleanup_eth();
        return;
    }
    eth_started = true;

    // 等待链路稳定
    vTaskDelay(pdMS_TO_TICKS(ETH_START_WAIT_MS));

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0)
    {
        printf("socket fail (errno: %d)\n", errno);

        cleanup_eth();
        return;
    }

    int reuse_addr = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr)) < 0)
    {
        printf("[eth] setsockopt SO_REUSEADDR fail (errno: %d)\n", errno);
    }

    // 设置超时：防止 recvfrom 永久阻塞
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
    {
        printf("[eth] setsockopt SO_RCVTIMEO fail (errno: %d)\n", errno);
        cleanup_eth();
        return;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(ETH_SERVER_PORT);

    size_t total_size = 0;
    float total_time_s = 0;
    size_t bytes_this_time = 0;
    size_t packets_this_time = 0;
    int64_t start_time = esp_timer_get_time();
    int64_t last_print_time = start_time;
    int64_t current_time = 0;

    auto buffer = std::make_unique<char[]>(ETH_MAX_TRANSMIT_SIZE);

    // 使用esp32内存分配
    // auto buffer = std::unique_ptr<char[], std::function<void(char *)>>(
    //     (char *)heap_caps_malloc(ETH_MAX_TRANSMIT_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA),
    //     [](char *p)
    //     { heap_caps_free(p); });

    if (is_server)
    {
        EthPacketStats eth_stats;
        server_addr.sin_addr.s_addr = ESP_IP4TOADDR(192, 168, 0, 1);
        if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        {
            printf("bind fail (errno: %d)\n", errno);

            cleanup_eth();
            return;
        }
        printf("[eth server] listening on 192.168.0.1:%u...\n", static_cast<unsigned int>(ETH_SERVER_PORT));
        printf("[eth server] expect client 192.168.0.2:%u, packet size %u, header %u, sequence + checksum enabled\n",
               static_cast<unsigned int>(ETH_CLIENT_PORT),
               static_cast<unsigned int>(ETH_MAX_TRANSMIT_SIZE), static_cast<unsigned int>(ETH_TEST_HEADER_SIZE));

        while (!All_Exit_Test_Flag)
        {
            struct sockaddr_in source_addr;
            socklen_t addr_len = sizeof(source_addr);

            int len = recvfrom(sock, buffer.get(), ETH_MAX_TRANSMIT_SIZE, 0, (struct sockaddr *)&source_addr, &addr_len);

            if (len < 0)
            {
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    printf("[eth server] recvfrom fail (error: %d)\n", errno);
                }
            }
            else
            {
                if (eth_validate_test_packet(buffer.get(), len, &source_addr, &eth_stats) == true)
                {
                    bytes_this_time += len;
                    total_size += len;
                    packets_this_time++;
                }
            }

            current_time = esp_timer_get_time();
            if (current_time - last_print_time >= 3000000)
            {
                float time_elapsed_sec = (float)(current_time - last_print_time) / 1000000.0f;
                total_time_s += time_elapsed_sec;

                float speed_kbps = (float)bytes_this_time / 1024.0f / time_elapsed_sec;
                float speed_mbps = (float)bytes_this_time * 8.0f / 1024.0f / 1024.0f / time_elapsed_sec;

                printf("[eth server] receive size: %.2f kb | receive speed: %.2f kb/s (%.2f mbps) | total receive size: %.2f kb | packets: %zu | total packets: %zu | ignored packets: %zu\n",
                       (float)bytes_this_time / 1024.0f, speed_kbps, speed_mbps, (float)total_size / 1024.0f,
                       packets_this_time, eth_stats.packet_count, eth_stats.ignored_packet_count);

                bytes_this_time = 0;
                packets_this_time = 0;
                last_print_time = current_time;
            }

            vTaskDelay(pdMS_TO_TICKS(ETH_TASK_LOOP_DELAY_MS));
        }

        printf("[eth server] === result ===\n");
        printf("[eth server] total size: %.2f kb\n", total_size / 1024.0);
        printf("[eth server] total time %.3f s\n", total_time_s);
        printf("[eth server] avg speed: %.2f kb/s\n",
               (total_time_s > 0.0f) ? ((total_size / 1024.0) / total_time_s) : 0.0);
        printf("[eth server] packets: %zu | ignored packets: %zu | bad packets: %zu | next expected sequence: %lu\n",
               eth_stats.packet_count, eth_stats.ignored_packet_count, eth_stats.bad_packet_count,
               static_cast<unsigned long>(eth_stats.expected_sequence));
    }
    else
    {
        struct sockaddr_in client_addr;
        client_addr.sin_family = AF_INET;
        client_addr.sin_port = htons(ETH_CLIENT_PORT);
        client_addr.sin_addr.s_addr = ESP_IP4TOADDR(192, 168, 0, 2);
        if (bind(sock, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0)
        {
            printf("[eth client] bind 192.168.0.2:%u fail (errno: %d)\n",
                   static_cast<unsigned int>(ETH_CLIENT_PORT), errno);

            cleanup_eth();
            return;
        }

        server_addr.sin_addr.s_addr = ESP_IP4TOADDR(192, 168, 0, 1);
        printf("[eth client] bind 192.168.0.2:%u\n", static_cast<unsigned int>(ETH_CLIENT_PORT));
        printf("[eth client] sending to 192.168.0.1:%u...\n", static_cast<unsigned int>(ETH_SERVER_PORT));
        printf("[eth client] packet size %u, header %u, sequence + checksum enabled\n",
               static_cast<unsigned int>(ETH_MAX_TRANSMIT_SIZE), static_cast<unsigned int>(ETH_TEST_HEADER_SIZE));

        size_t send_error_count = 0;
        size_t send_nomem_count = 0;
        uint32_t send_sequence = 0;

        while (!All_Exit_Test_Flag)
        {
            eth_build_test_packet(buffer.get(), ETH_MAX_TRANSMIT_SIZE, send_sequence);
            int len = sendto(sock, buffer.get(), ETH_MAX_TRANSMIT_SIZE, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));

            if (len < 0)
            {
                int send_errno = errno;
                send_error_count++;
                if (send_errno == ENOMEM)
                {
                    send_nomem_count++;
                    vTaskDelay(pdMS_TO_TICKS(ETH_SEND_BACKOFF_DELAY_MS));
                }
                else
                {
                    printf("[eth client] send error: %d\n", send_errno);
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
            }
            else
            {
                bytes_this_time += len;
                total_size += len;
                packets_this_time++;
                if (len == ETH_MAX_TRANSMIT_SIZE)
                {
                    send_sequence++;
                }
                else
                {
                    send_error_count++;
                    printf("[eth client] partial send: %d / %u bytes\n",
                           len, static_cast<unsigned int>(ETH_MAX_TRANSMIT_SIZE));
                }
            }

            // 定时打印进度（每秒）
            current_time = esp_timer_get_time();
            if (current_time - last_print_time >= 3000000)
            {
                float time_elapsed_sec = (float)(current_time - last_print_time) / 1000000.0f;
                total_time_s += time_elapsed_sec;

                float speed_kbps = (float)bytes_this_time / 1024.0f / time_elapsed_sec;
                float speed_mbps = (float)bytes_this_time * 8.0f / 1024.0f / 1024.0f / time_elapsed_sec;

                printf("[eth client] send size: %.2f kb | send speed: %.2f kb/s (%.2f mbps) | [eth client] total send size: %.2f kb | packets: %zu | next sequence: %lu | send errors: %zu, nomem: %zu\n",
                       (float)bytes_this_time / 1024.0f, speed_kbps, speed_mbps, (float)total_size / 1024.0f,
                       packets_this_time, static_cast<unsigned long>(send_sequence),
                       send_error_count, send_nomem_count);

                bytes_this_time = 0;
                packets_this_time = 0;
                send_error_count = 0;
                send_nomem_count = 0;
                last_print_time = current_time;
            }

            vTaskDelay(pdMS_TO_TICKS(ETH_TASK_LOOP_DELAY_MS));
        }

        printf("[eth client] === result ===\n");
        printf("[eth client] total size: %.2f kb\n", total_size / 1024.0);
        printf("[eth client] total time %.3f s\n", total_time_s);
        printf("[eth client] avg speed: %.2f kb/s\n",
               (total_time_s > 0.0f) ? ((total_size / 1024.0) / total_time_s) : 0.0);
        printf("[eth client] next sequence: %lu\n", static_cast<unsigned long>(send_sequence));
    }

    cleanup_eth();
}

void rs485_test_task(void *pv)
{
    bool is_send = (bool)pv;
    printf("rs485_test_task (%s) start\n", is_send ? "send" : "receive");

    size_t total_size = 0, total_time_us = 0;
    size_t bytes_this_time = 0;
    size_t last_print_wall_clock = esp_timer_get_time();

    if (is_send == true)
    {
        std::string payload(RS485_MAX_TRANSMIT_SIZE, 'D');
        while (All_Exit_Test_Flag == false)
        {
            size_t t1 = esp_timer_get_time();
            int32_t len = Uart_Bus->write(payload.data(), payload.size());
            size_t t2 = esp_timer_get_time();

            bytes_this_time += len;
            total_size += len;
            total_time_us += (t2 - t1);

            size_t now = esp_timer_get_time();
            if (now - last_print_wall_clock >= 3000000)
            {
                float speed_kbps = (float)bytes_this_time / 1024.0f / ((float)(now - last_print_wall_clock) / 1000000.0);

                printf("[rs485 send] send size: %.2f kb | send speed: %.2f kb/s | total send size: %.2f kb \n",
                       (float)bytes_this_time / 1024.0, speed_kbps, (float)total_size / 1024.0);

                bytes_this_time = 0;
                last_print_wall_clock = now;
            }

            vTaskDelay(pdMS_TO_TICKS(10));
        }

        float total_time_s = total_time_us / 1000000.0;

        printf("[rs485 send] === result ===\n");
        printf("[rs485 send] total size: %.2f kb\n", (float)total_size / 1024.0);
        printf("[rs485 send] total time %.3f s\n", total_time_s);
        printf("[rs485 send] avg speed: %.2f kb/s\n", ((float)total_size / 1024.0) / total_time_s);
    }
    else
    {
        while (All_Exit_Test_Flag == false)
        {
            size_t len = Uart_Bus->get_rx_buffer_length();
            if (len > 0)
            {
                auto buffer = std::make_unique<char[]>(len);

                // 使用esp32内存分配
                // auto buffer = std::unique_ptr<char[], std::function<void(char *)>>(
                //     (char *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA),
                //     [](char *p)
                //     { heap_caps_free(p); });

                size_t t1 = esp_timer_get_time();
                Uart_Bus->read(buffer.get(), len);
                size_t t2 = esp_timer_get_time();

                if (validate_data("rs485 receive", buffer.get(), len, 'D') == true)
                {
                    bytes_this_time += len;
                    total_size += len;
                    total_time_us += (t2 - t1);
                }
            }

            size_t now = esp_timer_get_time();
            if (now - last_print_wall_clock >= 3000000)
            {
                float speed_kbps = (float)bytes_this_time / 1024.0f / ((float)(now - last_print_wall_clock) / 1000000.0);

                printf("[rs485 receive] receive size: %.2f kb | receive speed: %.2f kb/s | total receive size: %.2f kb \n",
                       (float)bytes_this_time / 1024.0, speed_kbps, (float)total_size / 1024.0);

                bytes_this_time = 0;
                last_print_wall_clock = now;
            }

            vTaskDelay(pdMS_TO_TICKS(10));
        }

        float total_time_s = total_time_us / 1000000.0;

        printf("[rs485 receive] === result ===\n");
        printf("[rs485 receive] total size: %.2f kb\n", (float)total_size / 1024.0);
        printf("[rs485 receive] total time %.3f s\n", total_time_s);
        printf("[rs485 receive] avg speed: %.2f kb/s\n", ((float)total_size / 1024.0) / total_time_s);
    }

    printf("[rs485] task completed\n");
    Rs485_Test_Task_Handle = nullptr;
    vTaskDelete(NULL);
}

bool parse_cmd(std::vector<std::string> cmd)
{
    if (cmd.empty() == true)
    {
        printf("parse_cmd fail (cmd is empty)\n");
        return false;
    }
    if (cmd[0] == "all_exit")
    {
        All_Exit_Test_Flag = true;
        printf("all exit test\n");
        return true;
    }

    if (cmd[0] == "wifi")
    {
        if (Wifi_Download_Test_Task_Handle == nullptr)
        {
            All_Exit_Test_Flag = false;

            xTaskCreate(wifi_time_test_task, "wifi_task", 1024 * 8, NULL, 5, &Wifi_Download_Test_Task_Handle);
            return true;
        }
        else
        {
            eTaskState status = eTaskGetState(Wifi_Download_Test_Task_Handle);
            if (status == eDeleted)
            {
                All_Exit_Test_Flag = false;

                xTaskCreate(wifi_time_test_task, "wifi_task", 1024 * 8, NULL, 5, &Wifi_Download_Test_Task_Handle);
                return true;
            }
            else
            {
                printf("wifi_time_test_task create fail (status: %d)\n", status);
                return false;
            }
        }
    }
    else if (cmd[0] == "eth")
    {
        if (cmd.size() <= 1)
        {
            printf("parse_cmd fail (error: eth cmd)\n");
            return false;
        }
        else if (cmd[1] == "server")
        {
            if (Eth_Test_Task_Handle == nullptr)
            {
                All_Exit_Test_Flag = false;
                bool is_server = true;

                xTaskCreate(eth_test_task, "eth_task", 1024 * 8, (void *)is_server, 5, &Eth_Test_Task_Handle);
                return true;
            }
            else
            {
                eTaskState status = eTaskGetState(Eth_Test_Task_Handle);
                if (status == eDeleted)
                {
                    All_Exit_Test_Flag = false;
                    bool is_server = true;

                    xTaskCreate(eth_test_task, "eth_task", 1024 * 8, (void *)is_server, 5, &Eth_Test_Task_Handle);
                    return true;
                }
                else
                {
                    printf("eth_test_task create fail (status: %d)\n", status);
                    return false;
                }
            }
        }
        else if (cmd[1] == "client")
        {
            if (Eth_Test_Task_Handle == nullptr)
            {
                All_Exit_Test_Flag = false;
                bool is_server = false;

                xTaskCreate(eth_test_task, "eth_task", 1024 * 8, (void *)is_server, 5, &Eth_Test_Task_Handle);
                return true;
            }
            else
            {
                eTaskState status = eTaskGetState(Eth_Test_Task_Handle);
                if (status == eDeleted)
                {
                    All_Exit_Test_Flag = false;
                    bool is_server = false;

                    xTaskCreate(eth_test_task, "eth_task", 1024 * 8, (void *)is_server, 5, &Eth_Test_Task_Handle);
                    return true;
                }
                else
                {
                    printf("eth_test_task create fail (status: %d)\n", status);
                    return false;
                }
            }
        }
    }
    else if (cmd[0] == "rs485")
    {
        if (cmd.size() <= 1)
        {
            printf("parse_cmd fail (error: rs485 cmd)\n");
            return false;
        }
        else if (cmd[1] == "send")
        {
            if (Rs485_Test_Task_Handle == nullptr)
            {
                All_Exit_Test_Flag = false;
                bool is_send = true;

                xTaskCreate(rs485_test_task, "rs485_task", 1024 * 4, (void *)is_send, 5, &Rs485_Test_Task_Handle);
                return true;
            }
            else
            {
                eTaskState status = eTaskGetState(Rs485_Test_Task_Handle);
                if (status == eDeleted)
                {
                    All_Exit_Test_Flag = false;
                    bool is_send = true;

                    xTaskCreate(rs485_test_task, "rs485_task", 1024 * 4, (void *)is_send, 5, &Rs485_Test_Task_Handle);
                    return true;
                }
                else
                {
                    printf("rs485_test_task create fail (status: %d)\n", status);
                    return false;
                }
            }
        }
        else if (cmd[1] == "receive")
        {
            if (Rs485_Test_Task_Handle == nullptr)
            {
                All_Exit_Test_Flag = false;
                bool is_send = false;

                xTaskCreate(rs485_test_task, "rs485_task", 1024 * 4, (void *)is_send, 5, &Rs485_Test_Task_Handle);
                return true;
            }
            else
            {
                eTaskState status = eTaskGetState(Rs485_Test_Task_Handle);
                if (status == eDeleted)
                {
                    All_Exit_Test_Flag = false;
                    bool is_send = false;

                    xTaskCreate(rs485_test_task, "rs485_task", 1024 * 4, (void *)is_send, 5, &Rs485_Test_Task_Handle);
                    return true;
                }
                else
                {
                    printf("rs485_test_task create fail (status: %d)\n", status);
                    return false;
                }
            }
        }
    }

    printf("parse_cmd fail (unknown command)\n");
    printf("operation command:\n");
    printf("[all_exit:]\n");
    printf("[wifi:]\n");
    printf("[eth:server:] or [eth:client:]\n");
    printf("[rs485:send:] or [rs485:receive:]\n");

    return false;
}

extern "C" void app_main(void)
{
    printf("Ciallo\n");
    std::stringstream ss;
    ss << "[T-Spe_" << BOARD_VERSION << "][" << SOFTWARE_NAME << "]_firmware_" << get_software_build_time() << "\n";
    printf("%s", ss.str().c_str());

    ESP32->pin_mode(GPIO0_50MHZ_SWITCH, Cpp_Bus_Driver::Tool::Pin_Mode::OUTPUT);
    ESP32->pin_mode(LAN8671_WAKE_UP, Cpp_Bus_Driver::Tool::Pin_Mode::OUTPUT);

    ESP32->pin_write(GPIO0_50MHZ_SWITCH, 1);
    ESP32->pin_write(LAN8671_WAKE_UP, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP32->pin_write(LAN8671_WAKE_UP, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP32->pin_write(LAN8671_WAKE_UP, 1);

    Uart_Bus->begin();

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    esp_wifi_set_mode(WIFI_MODE_STA);

    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password) - 1);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    uart_driver_install(UART_NUM_0, 2048, 0, 0, NULL, 0);

    vTaskDelay(pdMS_TO_TICKS(500));

    printf("operation command:\n");
    printf("[all_exit:]\n");
    printf("[wifi:]\n");
    printf("[eth:server:] or [eth:client:]\n");
    printf("[rs485:send:] or [rs485:receive:]\n");

    while (1)
    {
        size_t uart_len = 0;
        uart_get_buffered_data_len(UART_NUM_0, &uart_len);
        if (uart_len > 0)
        {
            auto buffer = std::make_unique<char[]>(uart_len + 1);

            // 使用esp32内存分配
            // auto buffer = std::unique_ptr<char[], std::function<void(char *)>>(
            //     (char *)heap_caps_malloc(uart_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA),
            //     [](char *p)
            //     { heap_caps_free(p); });

            int read_len = uart_read_bytes(UART_NUM_0, buffer.get(), uart_len, pdMS_TO_TICKS(20));
            buffer[read_len] = '\0';
            std::string content(buffer.get());

            std::stringstream ss(content);
            std::string token;
            std::vector<std::string> parts;
            while (std::getline(ss, token, ':'))
            {
                parts.push_back(token);
            }

            parse_cmd(parts);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
