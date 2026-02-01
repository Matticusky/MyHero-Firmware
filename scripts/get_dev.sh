# warn that this script should be sourced, not executed
# Works with both bash and zsh (macOS default)
if [ -n "$BASH_SOURCE" ]; then
    # Bash
    if [ "$0" = "$BASH_SOURCE" ]; then
        echo "This script should be sourced, not executed. Try running: source $BASH_SOURCE"
        exit 1
    fi
elif [ -n "$ZSH_VERSION" ]; then
    # Zsh
    if [ "$0" = "${(%):-%x}" ] 2>/dev/null || [ "$ZSH_EVAL_CONTEXT" = "toplevel" ]; then
        echo "This script should be sourced, not executed. Try running: source $0"
        exit 1
    fi
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