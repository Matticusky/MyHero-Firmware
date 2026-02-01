# python script to automate setting up a development environment for the project
# clones the ESP IDF and ESP ADF frameworks, checks out correct version and installs dependencies

import os
import subprocess
import platform

ADF_ORIGIN = "https://github.com/espressif/esp-adf.git"
TARGET_IDF_VERSION = "v5.5.2"
TARGET_ADF_VERSION = "release/v2.x"

def detect_os():
    os_name = platform.system()
    if os_name == "Linux" or os_name == "Windows":
        return os_name
    else:
        print("Unsupported OS")
        exit(1)

def run_command(cmd, error_msg):
    try:
        subprocess.run(cmd, check=True)
        return True
    except subprocess.CalledProcessError as e:
        print(error_msg)
        print(e)
        exit(1)

def add_safe_directory():
    pwd = os.getcwd()
    run_command(
        ["git", "config", "--global", "--add", "safe.directory", pwd],
        "Error fixing git dubious path warning"
    )
    print(f"Added safe directory: {pwd}")

os_name = detect_os()

# Step 1: Clone ADF repository
run_command(
    ["git", "clone", ADF_ORIGIN, "ADF"],
    "Error cloning ESP ADF repository"
)
print("Cloned ESP ADF repository")

# Step 2: cd into ADF and checkout target version
os.chdir("ADF")
add_safe_directory()

run_command(
    ["git", "checkout", TARGET_ADF_VERSION],
    "Error checking out correct version of ESP ADF"
)
print("Checked out correct version of ESP ADF")

# Step 3: Initialize ADF submodules (not recursive)
run_command(
    ["git", "submodule", "update", "--init"],
    "Error initializing ADF submodules"
)
print("Initialized ADF submodules")

# Step 4: cd into IDF and checkout target version
os.chdir("esp-idf")
add_safe_directory()

run_command(
    ["git", "checkout", TARGET_IDF_VERSION],
    "Error checking out correct version of ESP IDF"
)
print("Checked out correct version of ESP IDF")

# Step 5: Initialize IDF submodules
run_command(
    ["git", "submodule", "update", "--init", "--recursive"],
    "Error initializing IDF submodules"
)
print("Initialized IDF submodules")

# Step 6: Go back to ADF and run install script
os.chdir("..")

if os_name == "Linux":
    run_command(["./install.sh"], "Error installing ADF dependencies")
elif os_name == "Windows":
    run_command(["install.bat"], "Error installing ADF dependencies")

print("Installed ESP ADF dependencies")

# Change back to project root directory
os.chdir("..")

print("Development environment set up successfully")
exit(0)
