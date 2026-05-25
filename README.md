# Pico HTTPS FreeRTOS example

An HTTPS client example running FreeRTOS for the Raspberry Pi Pico W and Pico2 W.

Implemented in C, leveraging the [Raspberry Pi Pico C/C++ SDK][pico-sdk] and Raspberry Pi port of [FreeRTOS].

[pico-sdk]: https://www.raspberrypi.com/documentation/pico-sdk/
[FreeRTOS]: https://www.freertos.org/Documentation/00-Overview

## Description

HTTPS examples for the Raspberry Pi Pico W are less common than HTTP client examples, largely because HTTPS adds the extra complexity of TLS, including cryptographic setup and certificate handling. HTTPS examples which run under FreeRTOS on Pico W are even rarer.

This repository contains a complete example C application that runs under FreeRTOS and uses separate tasks to send HTTPS requests, receive responses, monitor network status and monitor task health.

## Requirements

* [Raspberry Pi Pico C/C++ SDK][pico-sdk-repo] (tested against 2.2.0)
* [Raspberry Pi FreeRTOS][FreeRTOS-repo]
* [ARM toolchain][ARM-toolchain]
* [CMake]
* [Ninja]
* Native C/C++ build system for development platform
* Python (Rel 3.x) for development platform
* [Doxygen] to generate html code documentation _(optional)_

[pico-sdk-repo]: https://github.com/raspberrypi/pico-sdk.git
[FreeRTOS-repo]: https://github.com/raspberrypi/FreeRTOS-Kernel.git
[ARM-toolchain]: https://github.com/dwelch67/raspberrypi-pico/blob/main/TOOLCHAIN
[CMake]: https://cmake.org/
[Ninja]: https://ninja-build.org/
[Doxygen]: https://www.doxygen.nl/index.html

## Building

### Configuration

The following minimum build-time configuration is required for correct execution;

**PICO_BOARD** must be set to one of:
- pico
- pico_w
- pico2   _or_
- pico2_w

For this project **PICO_BOARD** must be either **pico_w** or **pico2_w**

**PICO_BUILD_TYPE** must be set to one of:
- Debug
- Release
- RelWithDebInfo   _or_
- MinSizeRel


**Example board and build type configuration for Linux and Windows:-**
#### Linux


```shell
export PICO_SDK_PATH=${HOME}/.pico/pico-sdk
export FREERTOS_SRC_PATH=${HOME}/.pico/FreeRTOS-Kernel

export PICO_BOARD="pico_w"
export PICO_BUILD_TYPE=Debug
```

<br>
</br>

#### Windows Developer Command Prompt for VS18
```shell
set "PICO_SDK_PATH=%USERPROFILE%\.pico-sdk\sdk\2.2.0"
set "FREERTOS_SRC_PATH=%USERPROFILE%\.pico-sdk\FreeRTOS-Kernel"
set "PICO_TOOLCHAIN_PATH=%USERPROFILE%\.pico-sdk\toolchain\14_2_Rel1"
set "PICO_BOARD=pico_w"
set "PICO_BUILD_TYPE=Debug"
```

<br>
</br>

#### Windows Developer Powershell for VS18
You might need to set Execution-Policy to allow setting variables either in the shell or in a .ps1 script:
```shell
Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy Bypass
```

```shell
$env:PICO_SDK_PATH = Join-Path $env:USERPROFILE '.pico-sdk\sdk\2.2.0'
$env:FREERTOS_SRC_PATH = Join-Path $env:USERPROFILE '.pico-sdk\FreeRTOS-Kernel'
$env:PICO_TOOLCHAIN_PATH = Join-Path $env:USERPROFILE '.pico-sdk\toolchain\14_2_Rel1'
$env:PICO_BOARD = 'pico_w'
$env:PICO_BUILD_TYPE = 'Debug'
```

<br>

</br>

**Now** set your wireless network SSID and its password. These will be cached in a header file in the build directory which is generated during build. The cached files are removed when the build directory is deleted. The header generation is done by [mkssidheader](scripts/mkssidheader.sh) on Linux or [mkssidheader](scripts/mkssidheader.ps1) on Windows'.

Note that any shell variables which are used as '-D' arguments in cmake will be visible in CMakeCache.txt in the build directory. The generated header files will be confined to the current build environment. They will not be included in any source code commits.

The web server hostname is cached in another header file in the build directory which is generated during build. Included in this file will be the CA cerificate for the web server. This CA certificate is in a C character string, suitable for use as an argument for `altcp_tls_create_config_client()` and is generated automatically during build. The header generation is done by [mkcaheader](scripts/mkcaheader.sh) on Linux or [mkcaheader](scripts/mkcaheader.ps1) on Windows'.

#### Linux
```shell
export PICO_WIFI_SSID="MySSID"
export PICO_WIFI_PASSWORD="MyWiFiPassword"

export HTTPS_HOST="example.com"
```

<br>
</br>

#### Windows Developer Command Prompt for VS18
```shell
set "PICO_WIFI_SSID=MySSID"
set "PICO_WIFI_PASSWORD=MyWiFiPassword"
set "HTTPS_HOST=example.com"
```

<br>
</br>

#### Windows Developer Powershell for VS18
```shell
$env:PICO_WIFI_SSID = 'MySSID'
$env:PICO_WIFI_PASSWORD = 'MyWiFiPassword'
$env:HTTPS_HOST = 'example.com'
```



The configurations for the `lwIP` and `Mbed TLS` libraries are contained in [lwipopts.h](include/lwipopts.h) and [mbedtls_config.h](include/mbedtls_config.h). These should work as-is, but can be tuned to suit.

<br>
</br>

### Compilation

#### Linux
Delete build directory if a clean build is desired:
```shell
rm -fr build
```
First time, or after deleting build directory:
```shell
mkdir build

cmake -S . -B build/${PICO_BUILD_TYPE} \
           -G Ninja \
           -DPICO_BOARD=${PICO_BOARD} \
           -DCMAKE_BUILD_TYPE=${PICO_BUILD_TYPE}
```
The following command should be run to build image or rebuild after code changes:
```shell
ninja -C build/${PICO_BUILD_TYPE}
```
After a successful build the `ELF` and `UF2` files will be in `build/${PICO_BUILD_TYPE}`

<br>
</br>

#### Windows Developer Command Prompt for VS18
Delete build directory if a clean build is desired:
```shell
rmdir build /s /q 
```
First time, or after deleting build directory:
```shell
mkdir build

cmake -S . -B build\%PICO_BUILD_TYPE% -G Ninja ^
-DPICO_BOARD=%PICO_BOARD% ^
-DCMAKE_BUILD_TYPE=%PICO_BUILD_TYPE%
```
The following command should be run to build image or rebuild after code changes:
```shell
ninja -C build/%PICO_BUILD_TYPE%
```
After a successful build the `ELF` and `UF2` files will be in `build\%PICO_BUILD_TYPE%`

<br>
</br>

#### Windows Developer PowerShell for VS18
Delete build directory if a clean build is desired:
```shell
remove-item -path build -recurse -force
```
First time, or after deleting build directory:
```shell
mkdir build

cmake -S . -B "build\${env:PICO_BUILD_TYPE}" -G Ninja `
  "-DPICO_BOARD=${env:PICO_BOARD}" `
  "-DCMAKE_BUILD_TYPE=${env:PICO_BUILD_TYPE}"
```
The following command should be run to build image or rebuild after code changes:
```shell
ninja -C build/${env:PICO_BUILD_TYPE}
```
After a successful build the `ELF` and `UF2` files will be in `build/${env:PICO_BUILD_TYPE}`




## Overview

This application starts by initialising Pico SDK standard I/O. Output will be sent through USB or UART depending on `pico_enable_stdio_uart()` setting in [CMakeLists.txt](CMakeLists.txt). The memory monitor task is started in `Debug` loads. Every minute this will print (stack) memory usage per task, as well as heap usage. It will also print circular buffer indices and state.

The **main task** is started. This initialises the wireless driver and connects to the configured SSID. On success it starts the remaining tasks:
- **check net task** - checks WiFi status every 10 seconds and pings (ICMP) gateway every minute.
- **https task**
	+ initiates a TLS connection to configured web server, default URL is `HTTPS_HOST://`, but this may be overriden by creating `include/private_defs.h` containing `#define HTTPS_REQUEST  "..."`. This header file may also be used for preprocessor definitions which contain proprietary or prvate data or information. `include/private/*.h` files are excluded from git commits.
	+ The ALTCP connection is managed in the tcpip thread for reasons explained in the `http_task()` function header comment block. This is the nub for getting TLS to work in FreeRTOS. The altcp callbacks must be setup and run within the tcpip thread for them to work.
	+ The ALTCP callback functions communicate with the https task via a message queue and a circular buffer. When a complete https reply has been received it is read from the buffer and printed.
	+ After sleeping for a minute the https task will repeat the steps above until the end of time.
- **blink task** - blinks onbaord LED every other second.
- **watchdog task** - checks the watchdog flag for each og=f the other tasks. A `software_reset()` will be done if any task misses `MAX_WATCHDOG_MISS_COUNT`.

## Recommended Reading

[Master the Raspberry Pi Pico in C: WiFi with LwIP, MbedTLS & FreeRTOS](https://iopress.info/index.php/books/pico/master-the-raspberry-pi-pico-in-c-wifi-with-lwip-mbedtls-freertos-2e) is an excellent guide to programming the Raspberry Pi Pico. This book concentrates on wireless networking. A more general introduction to Pico software development in C is:

[Programming the Raspberry Pi Pico/W in C](https://iopress.info/index.php/books/pico/programming-the-raspberry-pi-pico-w-in-c-3ed)

These books, explain C development for Pico better than anything I have been able to find elsewhere.

A big thank you to [Marcelo Alcocer](https://github.com/marceloalcocer) for his well written and explained [picohttps](https://github.com/marceloalcocer/picohttps) example. This is a good example of how to use TLS in a non-RTOS system.

## References

* [Pico SDK Documentation](https://www.raspberrypi.com/documentation/pico-sdk/)
* [FreeRTOS Documentation](https://www.freertos.org/Documentation/00-Overview)
* [lwIP documentation](https://www.nongnu.org/lwip/2_1_x/index.html)
* [Mbed TLS documentation](https://mbed-tls.readthedocs.io/en/latest/)


