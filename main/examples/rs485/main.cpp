/*
 * @Description: iic_scan
 * @Author: LILYGO_L
 * @Date: 2025-06-13 12:06:14
 * @LastEditTime: 2026-01-28 15:07:23
 * @License: GPL 3.0
 */
#include "sdkconfig.h"
#include "lilygo_device_driver_library.h"
#include "cpp_bus_driver_library.h"

size_t Cycle_Time = 0;

auto Uart_Bus = std::make_shared<Cpp_Bus_Driver::Hardware_Uart>(RS485_RX, RS485_TX, uart_port_t::UART_NUM_1);

auto ESP32 = std::make_unique<Cpp_Bus_Driver::Tool>();

extern "C" void app_main(void)
{
    printf("Ciallo\n");

    Uart_Bus->begin();

    while (1)
    {
        if (ESP32->get_system_time_ms() > Cycle_Time)
        {
            std::string data = "Ciallo\n";
            Uart_Bus->write(data.data(), data.size());
            
            printf("rs485 send data\n");
            Cycle_Time= ESP32->get_system_time_ms() + 1000;
        }

        size_t buffer_length = Uart_Bus->get_rx_buffer_length();
        if (buffer_length > 0)
        {
            auto buffer = std::make_unique<uint8_t[]>(buffer_length + 1);

            if (Uart_Bus->read(buffer.get(), buffer_length) == buffer_length)
            {
                buffer[buffer_length + 1] = '\0';
                printf("received data: [%s]\n", buffer.get());
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
