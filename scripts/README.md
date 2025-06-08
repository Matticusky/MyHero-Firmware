## Automation Scripts ##
This document enlists usage guide for automation scripts for project setup and deployment.
Scripts are written in Python and Bash and are included inside the `scripts` directory. All scripts are tested on Windows and Linux systems. You should execute all scripts from the root directory of the project. On Windows it expects to be run from CMD not from PowerShell.

### Prerequisites ###
- Python 3.6 or higher
- Git
- Windows/Linux system
- scipts/secrets.txt file with AWS credentials
- No white spaces in the path of the project directory
- set correct versions in `data/HW_VERSIONS.txt` file and `main/Device/device_manager.c file`

### Adding AWS Credentials ###
- Add AWS credentials in `scripts/secrets.txt` file in the following format:
    ```txt
    safjdsgfsbavldsjagfafgi
    ```
- User should create and add key manually in the file. This key will be used to fetch AWS credentials in the scripts. This file is not tracked by git.

### Usage Guide ###
1. Setting up DEV environment:
    - Run the following command to setup the DEV environment in linux  and windows systems:
        ```bash
        python scripts/setup_dev.py
        ```
    - This script will setup correct version of ESP IDF, ESP ADF and install their dependencies
2. Fetching Environment Variables:
    - Run the following command to get environment variables set in windows systems:
        ```bash
        ADF\export.bat
        ```
    - Run the following command to get environment variable in linux systems:
        ```bash
        source scripts/get_dev.sh
        ```
    - You'll have now necessary environment variables setup for project building and flashing.


3. Provisioning a new device:
    > [!WARNING]
    > Please note that this script is intended for mass programming field installation devices. Do not use for test devices. It will create and provision a new device in backend.See reprovisioning section and reuse existing device ID for test devices.

    - Run the following command to provision a new device:
        ```bash
        python scripts/flash_new_device.py <PORT>
        ```
        Replace port with the port number of the device.
        For example in linux it could be `/dev/ttyUSB0`
        ```bash
        python scripts/flash_new_device.py /dev/ttyUSB0
        ```
        And in windows it could be `COM3`
        ```bash
        python scripts/flash_new_device.py COM3
        ```
    - You should fetch Environment Variables before running this script as outlined in step 2.
    - This script will provision a new device. Fetches new device ID and certificates and builds project with that credetials as well as uploads to device. On each call it will automatically setup different device ID and certificates. Intended to be used for mass provisioning of devices.

4. Reprovisioning an existing device:
    - Run the following command to reprovision an existing device:
        ```bash
        python scripts/flash_device.py  <DEVICE_ID> <PORT>
        ```
        Replace device ID with the device ID of the device and port with the port number of the device.
        For example in linux it could be `/dev/ttyUSB0`
        ```bash
        python scripts/flash_device.py W0101 /dev/ttyUSB0
        ```
        And in windows it could be `COM3`
        ```bash
        python scripts/flash_device.py W0101 COM3
        ```
    - You should fetch Environment Variables before running this script as outlined in step 2.
    - This script will provision an existing device. Fetches existing certificates for given device ID and builds project with that credetials as well as uploads to device. Intended to be used for reprovisioning of devices.

