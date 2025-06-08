# python script to automate setting up a development environment for the project
# clones the ESP IDF and ESP ADF frameworks, checks out correct version and installs dependencies

import os
import subprocess
import platform

ADF_ORIGIN = "https://github.com/espressif/esp-adf.git"
TARGET_IDF_VERSION = "6568f8c553f89c01c101da4d6c735379b8221858"
TARGET_ADF_VERSION = "8a3b56a9b65af796164ebffc4e4bc45f144760b3"

# detect OS, continue if it is linux or windows, exit otherwise

def detect_os():
    os_name = platform.system()
    if os_name == "Linux" or os_name == "Windows":
        return os_name
    else:
        print("Unsupported OS")
        exit(1)


os_name=detect_os()

# clone the ESP ADF repository, IDF is included as a submodule
# clone to ADF directory
try:
    subprocess.run(["git", "clone", ADF_ORIGIN, "ADF"], check=True)
    print("Cloned ESP ADF repository")
except subprocess.CalledProcessError as e:
    print("Error cloning ESP ADF repository")
    print(e)
    exit(1)
    
# change directory to ADF
os.chdir("ADF")

# should also fix git dubious path warning
try:
    pwd = os.getcwd()
    subprocess.run(["git", "config", "--global", "--add", "safe.directory",pwd], check=True)
    print("Fixed git dubious path warning")
except subprocess.CalledProcessError as e:
    print("Error fixing git dubious path warning")
    print(e)
    exit(1)
    
    
# initialize and update submodules
try:
    subprocess.run(["git", "submodule", "update", "--init", "--recursive"], check=True)
    print("Initialized and updated submodules")
except subprocess.CalledProcessError as e:
    print("Error initializing and updating submodules")
    print(e)
    exit(1)
    

    
# check out the correct version of ESP IDF
os.chdir("esp-idf")
# fix git dubious path warning
try:
    pwd = os.getcwd()
    subprocess.run(["git", "config", "--global", "--add", "safe.directory",pwd], check=True)
    print("Fixed git dubious path warning")
except subprocess.CalledProcessError as e:
    print("Error fixing git dubious path warning")
    print(e)
    exit(1)
    
try:
    subprocess.run(["git", "checkout", TARGET_IDF_VERSION], check=True)
    print("Checked out correct version of ESP IDF")
except subprocess.CalledProcessError as e:
    print("Error checking out correct version of ESP IDF")
    print(e)
    exit(1)
# Install ESP IDF dependencies
# scripts have .sh, .bat and .cmd extensions for linux, windows and mac respectively
if os_name == "Linux":
    try:
        subprocess.run(["./install.sh"], check=True)
        print("Installed ESP IDF dependencies")
    except subprocess.CalledProcessError as e:
        print("Error installing ESP IDF dependencies")
        print(e)
        exit(1)

        
elif os_name == "Windows":
    try:
        subprocess.run(["install.bat"], check=True)
        print("Installed ESP IDF dependencies")
    except subprocess.CalledProcessError as e:
        print("Error installing ESP IDF dependencies")
        print(e)
        exit(1)

        

        
# change back to ADF directory
os.chdir("..")
# install ADF dependencies
if os_name == "Linux":
    try:
        subprocess.run(["./install.sh"], check=True)
        print("Installed ESP ADF dependencies")
    except subprocess.CalledProcessError as e:
        print("Error installing ESP ADF dependencies")
        print(e)
        exit(1)
elif os_name == "Windows":
    try:
        subprocess.run(["install.bat"], check=True)
        print("Installed ESP ADF dependencies")
    except subprocess.CalledProcessError as e:
        print("Error installing ESP ADF dependencies")
        print(e)
        exit(1)
# change back to project root directory
os.chdir("..")
        
# All done can exit now
print("Development environment set up successfully")
exit(0)