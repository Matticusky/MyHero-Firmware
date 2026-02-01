## Automation Scripts ##
This document enlists usage guide for automation scripts for project setup and deployment.
Scripts are written in Python and Bash and are included inside the `scripts` directory. All scripts are tested on Windows and Linux systems. You should execute all scripts from the root directory of the project. On Windows it expects to be run from CMD not from PowerShell.

### Prerequisites ###
- Python 3.6 or higher
- Git
- Windows/Linux system
- No white spaces in the path of the project directory

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

3. Building and Flashing:
    - After setting up the environment variables, you can build and flash the project using:
        ```bash
        idf.py build flash -p <PORT>
        ```
        Replace `<PORT>` with your device port (e.g., `/dev/ttyUSB0` on Linux or `COM3` on Windows).
