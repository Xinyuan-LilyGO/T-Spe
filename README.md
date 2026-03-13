<!--
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-01-26 09:31:39
 * @LastEditTime: 2026-03-13 16:49:27
 * @License: GPL 3.0
-->

<h1 align = "center">T-Spe</h1>

## **English | [中文](./README_CN.md)**

## VersionIteration:
| Version                               | Update date                       |Update description|
| :-------------------------------: | :-------------------------------: |:--------------: |
| T-Spe_V1.0                      | 2026-03-13                    |   Original version      |

## PurchaseLink

| Product                     | SOC           |  FLASH  |  PSRAM   | Link                   |
| :------------------------: | :-----------: |:-------: | :---------: | :------------------: |
| T-Spe_V1.0   | NULL |   NULL   | NULL |  [NULL]()   |

## Directory
- [Describe](#describe)
- [Preview](#preview)
- [Module](#module)
- [SoftwareDeployment](#SoftwareDeployment)
- [PinOverview](#pinoverview)
- [FAQ](#faq)
- [Project](#project)

## Describe

T-Spe is a compact controller board specifically designed for industrial multi-network communication. Built around the Espressif **ESP32** core, it integrates an onboard **Microchip LAN8671 10BASE-T1S Ethernet PHY** and an **RS485 transceiver**, combining emerging single-pair Ethernet technology with classic industrial fieldbuses. Featuring an ultra-wide 5–75V input voltage range and a seamless power switching circuit, the T-Spe can be reliably deployed in demanding environments such as factory automation, in-vehicle systems, and smart grids, serving as an edge computing node or a protocol conversion gateway.

## Preview
### Beta version test images

<p align="center" width="100%">
    <img src="image/1.jpg" alt="">
</p>

---

### Actual Product Image

## Module

### 1. Core Processor Module

* Chip: ESP32-WROVER-E
* FLASH: 16M
* PSRAM: 8M
* Related Documents:
    >[Espressif](https://documentation.espressif.com/esp32-wrover-e_esp32-wrover-ie_datasheet_en.html)

### 2. Ethernet

* Chip: LAN8671
* Communication Protocol: RMII
* Related Documents:
    >[LAN8671](./docs/LAN8671C2T-E-U3B.pdf)

### 3. RS485

* Module: TD301D485H-A
* Communication Protocol: UART
* Related Documents:
    >[TD301D485H-A](./docs/TD301D485H-A.pdf)
* Dependent Libraries:
    >[cpp_bus_driver](https://github.com/Llgok/cpp_bus_driver)

### 4. Step-Down Converter

* Chip: SY8513
* Additional Information: Supports input voltage 5-75V
* Related Documents:
    >[SY8513](./docs/DS_SY8513.pdf)

## SoftwareDeployment

### Examples Support

| example | `[vscode][esp-idf-v5.5.3]` | description | picture |
| ------  | ------ | ------ | ------ | 
| [general_test](./main/examples/general_test) |  <p align="center">![alt text][supported] |factory example | |
| [iperf_ethernet](./main/examples/iperf_ethernet) |  <p align="center">![alt text][supported] | | |
| [rs485](./main/examples/rs485) |  <p align="center">![alt text][supported] | | |
| [wifi](./main/examples/wifi) |  <p align="center">![alt text][supported] | | |
| [wifi_http_download_file](./main/examples/wifi_http_download_file) |  <p align="center">![alt text][supported] | | |

[supported]: https://img.shields.io/badge/-supported-green "example"

| firmware | description | picture |
| ------  | ------  | ------ |
| [general_test](./firmware/[T-Spe][general_test]) | factory example |  |

### ESP-IDF Visual Studio Code  
1. Install [Visual Studio Code](https://code.visualstudio.com/Download) by selecting the appropriate version for your operating system.  

2. Open the "Extensions" sidebar in Visual Studio Code (or use <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>X</kbd> to open extensions), search for the "ESP-IDF" extension, and install it.  

3. While the extension is installing, use the git command to clone the repository:  

        git clone --recursive https://github.com/Xinyuan-LilyGO/T-Spe.git

    Ensure you include the `--recursive` flag during cloning. If you forget to include it, you will need to initialize the submodules later by running:  

        git submodule update --init --recursive  

4. Download and install [ESP-IDF v5.5.3](https://dl.espressif.cn/dl/esp-idf/?idf=4.4). Take note of the installation path. Open the previously installed "ESP-IDF" extension and select "Configure ESP-IDF Extension." Choose the "USE EXISTING SETUP" menu, then select "Search ESP-IDF in system." Correctly configure the installation path you noted earlier:  
   - **Enter ESP-IDF directory (IDF_PATH):** `Your installation path xxx\Espressif\frameworks\esp-idf-v5.5.3`  
   - **Enter ESP-IDF Tools directory (IDF_TOOLS_PATH):** `Your installation path xxx\Espressif`  
    Click the "Install" button at the bottom right to proceed with the framework installation.  

5. Click the "SDK Configuration Editor" in the ESP-IDF extension menu at the bottom of Visual Studio Code. In the search bar, look for the field "Select the example to build" and choose the project you want to compile. Save the settings.  

6. Click "Set Espressif Device Target" in the bottom menu bar of Visual Studio Code and select **ESP32**. Next, click "Build Project" in the bottom menu bar and wait for the build to complete. Then, click "Select Port to Use," followed by "Flash Project" to upload the program.  

### firmware download
1. Open the project file "tools" and locate the ESP32 burning tool. Open it.

2. Select the correct burning chip and burning method, then click "OK." As shown in the picture, follow steps 1->2->3->4->5 to burn the program. If the burning is not successful, press and hold the "BOOT-0" button and then download and burn again.

3. Burn the file in the root directory of the project file "[firmware](./firmware/)" file,There is a description of the firmware file version inside, just choose the appropriate version to download.

<p align="center" width="100%">
    <img src="image/3.jpg" alt="example">
    <img src="image/11.png" alt="example">
</p>


## PinOverview

For pin definitions, please refer to the configuration file: 
<br />

[t_spe_config.h](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/main/src/device/t_spe/t_spe_config.h)  

## FAQ

* Q. After reading the above tutorials, I still don't know how to build a programming environment. What should I do?
* A. If you still don't understand how to build an environment after reading the above tutorials, you can refer to the [LilyGo-Document](https://github.com/Xinyuan-LilyGO/LilyGo-Document) document instructions to build it.

<br />

* Q. Why is my board continuously failing to download the program?
* A. Please hold down the "BOOT" button and try downloading the program again.

<br />

## Project
* [T-Spe_V1.0_202603131624](./project/T-Spe_V1.0_202603131624.pdf)

