# CoCo Storage

Non-volatile storage module for CoCo
Inspired by these implementations:
* [ESP-32 NVS](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html)
* [Zephyr NVS](https://docs.zephyrproject.org/latest/services/storage/nvs/nvs.html)

## Import
Add coco-storage/\<version> to your conanfile where version corresponds to the git tags

## Features
* Storage interface that can be implemented on top of on-chip flash, external flash or existing implementations
* Implementation for on-chip flash memory

## Supported Platforms
This module does not contain platform dependent code
