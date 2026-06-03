#这里选择你需要编译的项目
if(CONFIG_EXAMPLE_BUILD_GENERAL_TEST)
    set(project_path "examples/general_test/")
elseif(CONFIG_EXAMPLE_BUILD_WIFI)
    set(project_path "examples/wifi/")
elseif(CONFIG_EXAMPLE_BUILD_IPERF_ETHERNET)
    set(project_path "examples/iperf_ethernet/")
elseif(CONFIG_EXAMPLE_BUILD_RS485)
    set(project_path "examples/rs485/")
elseif(CONFIG_EXAMPLE_BUILD_WIFI_HTTP_DOWNLOAD_FILE)
    set(project_path "examples/wifi_http_download_file/")
endif()
        
idf_component_register(SRC_DIRS ${project_path} 
                        INCLUDE_DIRS ${project_path}
                        PRIV_REQUIRES
                        lilygo_device_driver
                        cpp_bus_driver
                        nvs_flash
                        esp_wifi
                        esp_event
                        esp_netif
                        esp_eth
                        lwip
                        ethernet_init
                        esp_timer
                        esp_app_format)
