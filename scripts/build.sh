##!/bin/bash
#
# build.sh - Compiles kernel and prepare python environment
#

set -e # Exit if any command fails

# --- 1. Path Detection ---
#SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)
#PROJECT_ROOT=$(cd -- "$SCRIPT_DIR/.." &> /dev/null && pwd)

# Define project paths
#KERNEL_DIR="$PROJECT_ROOT/kernel"
#USER_DIR="$PROJECT_ROOT/user"
#PY_REQ="$USER_DIR/cli/requirements.txt"
#VENV_DIR="$USER_DIR/.venv"

KERNEL_DIR="../kernel"
USER_DIR="../user"
PY_REQ="../cli/requirements.txt"
VENV_DIR="../user/.venv"

echo "=========================================="
echo "NXP Simtemp Build Script"
#echo "Project Root: $PROJECT_ROOT"
echo "=========================================="

# --- 2. Clean management ---
if [ "$1" == "clean" ]; then
    echo "--- Cleaning Kernel Module ---"
    (cd "$KERNEL_DIR" && make clean)
    
    echo "--- Cleaning Python Virtual Environment ---"
    if [ -d "$VENV_DIR" ]; then
        rm -rf "$VENV_DIR"
        echo "Removed $VENV_DIR"
    fi
    
    echo ""
    echo "--- Clean complete ---"
    exit 0
fi

# --- 3. Compile kernel module ---
echo "--- Building Kernel Module (nxp_simtemp) ---"

KDIR_DEFAULT="/lib/modules/$(uname -r)/build"
KDIR="${KDIR:-$KDIR_DEFAULT}"

if [ ! -d "$KDIR" ]; then
    echo "Error: Kernel headers not found at $KDIR"
    echo "Please install kernel headers for $(uname -r)"
    echo "e.g., sudo apt install linux-headers-$(uname -r)"
    exit 1
fi

(cd "$KERNEL_DIR" && make "$@")



echo ""
echo "--- Building User App (Python Environment) ---"

# --- 4. Python venv ---
if [ ! -d "$VENV_DIR" ]; then
    echo "Python virtual environment not found. Creating one..."
    
    # Check python3-venv
    if ! command -v python3-venv &> /dev/null; then
        echo "python3-venv package not found. Installing..."
        sudo apt-get install -y python3-venv
    fi
    
    # Check python3-tk (tkinter)
    if ! dpkg -s python3-tk &> /dev/null; then
        echo "python3-tk (tkinter) not found. Installing for GUI..."
        sudo apt-get install -y python3-tk
    fi

    python3 -m venv "$VENV_DIR"
    echo "Virtual environment created at $VENV_DIR"
else
    echo "Python virtual environment found."
fi

# Activate venv to install packages
PIP_EXEC="$VENV_DIR/bin/pip"

if [ -f "$PY_REQ" ]; then
    echo "Installing CLI requirements..."
    "$PIP_EXEC" install -r "$PY_REQ"
fi

echo ""
echo "--- Build complete ---"
echo "Kernel module: $KERNEL_DIR/nxp_simtemp.ko"
echo "User apps:     $USER_DIR/cli/main.py, $USER_DIR/gui/gui.py"
echo "Python venv:   $VENV_DIR"
echo ""
echo "To run CLI:    $VENV_DIR/bin/python3 $USER_DIR/cli/main.py"
echo "To run GUI:    sudo $VENV_DIR/bin/python3 $USER_DIR/gui/gui.py"