# warn that this script should be sourced, not executed
if [ "$0" = "$BASH_SOURCE" ]; then
    echo "This script should be sourced, not executed. Try running: source $BASH_SOURCE"
    exit 1
fi

# export esp-idf 
cd ADF/
# export ADF
export ADF_PATH=$(pwd)
. ./export.sh
# apply git patch
# "${ESP_PYTHON}" "../tools/adf_install_patches.py" apply-patch

# cd to ADF directory
# cd ../
# cd to project directory
cd ..
# all done exit
echo "ADF and ESP-IDF are now exported"