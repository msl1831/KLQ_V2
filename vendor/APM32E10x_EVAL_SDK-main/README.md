# APM32E10x EVAL SDK

Official evaluation-board SDK for the Geehy Semiconductor APM32E103 series.

## Release information

- SDK version: V1.0
- Release date: November 25, 2022
- Target board: APM32E103ZE EVAL Board
- Target device: APM32E103ZET6
- Supported toolchains: Keil MDK and IAR Embedded Workbench for Arm
- Official archive: `APM32E10x_EVAL_SDK_v1.0.zip`
- SHA-256: `b22263ac8107ebc9a18d83c29c469923de0a0390e173191051c4bdfb72dcd846`

## Repository structure

| Directory | Description |
| --- | --- |
| `Boards` | APM32E103 EVAL board support and peripheral drivers |
| `Documents` | Documents included in the official SDK package |
| `Examples` | Peripheral and board examples for ADC, CAN, EMMC, I2C, RTC, SDIO, SPI, USART, and USB |
| `Libraries` | APM32E10x standard peripheral drivers, CMSIS, device support, and USB device library |
| `Middlewares` | Third-party middleware included with the official package |

## Getting started

1. Install the APM32E1xx device support pack from the Geehy website.
2. Open an example project under `Examples` using Keil MDK or IAR Embedded Workbench for Arm.
3. Select the APM32E103ZET6 target and the appropriate debugger.
4. Build the project and program the APM32E103ZE EVAL Board.

Refer to `Readme.pdf` and the individual `readme.txt` files under `Examples` for board setup and example-specific instructions.

## License

Geehy-provided source code is governed by the original `GEEHY COPYRIGHT NOTICE.txt` included in this repository. The license limits use of that code to Geehy-manufactured devices and includes specific redistribution requirements.

Third-party components remain under their respective licenses, including:

- CMSIS: Apache License 2.0, preserved under `Libraries/CMSIS`
- FatFs: BSD-style license, preserved under `Middlewares/fat_fs`

See `Notice.txt` for the third-party notice supplied with the official SDK package. No repository-wide open-source license has been added.

## Official resources

- [APM32E103 product page](https://global.geehy.com/product/fifth/APM32E103)
- [APM32E103ZE EVAL Board](https://global.geehy.com/design/hardware_detail/61)
- [Geehy software development tools](https://global.geehy.com/design/software)
