#!/bin/bash
#
# run_demo.sh - Load, test, and unload the nxp_simtemp module.
#
set -e # Exit if anything fails

# We need to be root (sudo)
if [ "$(id -u)" -ne 0 ]; then
  echo "This script must be run with sudo."
  echo "Running: sudo $0 $@"
  exec sudo "$0" "$@"
fi

# --- 1. Path Detection (MEJORA) ---
# Esto hace el script robusto y ejecutable desde cualquier lugar.
#SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)
#PROJECT_ROOT=$(cd -- "$SCRIPT_DIR/.." &> /dev/null && pwd)

# --- 2. Definición de Rutas (MEJORA) ---
# Todas las rutas ahora se basan en $PROJECT_ROOT
MODULE_NAME="nxp_simtemp"
MODULE_KO="../kernel/$MODULE_NAME.ko"
CLI_APP="../user/cli/main.py"
BUILD_SCRIPT="../scripts/build.sh"
# (Usar el Python del venv que build.sh crea)
PYTHON_EXEC="../user/.venv/bin/python3" 

echo "--- (Step 1) Compiling everything ---"
# Usar la ruta absoluta al script de build
bash "$BUILD_SCRIPT"
if [ ! -f "$MODULE_KO" ]; then
    echo "Error: $MODULE_KO not found after compilation."
    exit 1
fi

echo ""
echo "--- (Step 2) Loading the module (insmod) ---"
# Clear logs
dmesg -C
insmod "$MODULE_KO"
sleep 0.5 # Wait for udev to create the node

# T1: Verify that the nodes exist
echo "Verifying device nodes..."
if [ -c "/dev/simtemp" ]; then
    echo "OK: /dev/simtemp exists."
else
    echo "FAIL: /dev/simtemp was not created."
    dmesg | tail -n 10
    exit 1
fi

if [ -f "/sys/class/simtemp/simtemp/sampling_ms" ]; then
    echo "OK: /sys/class/simtemp/simtemp/sampling_ms exists."
else
    echo "FAIL: sysfs was not created."
    dmesg | tail -n 10
    exit 1
fi
echo "Loading logs (dmesg):"
dmesg | tail -n 5

echo ""
echo "--- (Step 3) Running the Acceptance Test (T3/T5) ---"
echo "Running test with Python from venv: $PYTHON_EXEC"
if [ ! -f "$PYTHON_EXEC" ]; then
    echo "FAIL: Python venv not found. Did build.sh run correctly?"
    exit 1
fi

"$PYTHON_EXEC" "$CLI_APP" --test
TEST_RESULT=$?

if [ $TEST_RESULT -ne 0 ]; then
    echo "--- DEMO FAILED (FAIL) ---"
else
    echo "--- DEMO PASSED (PASS) ---"
fi

echo ""
echo "--- (Step 4) Unloading the module (rmmod) ---"
rmmod "$MODULE_NAME"
echo "Unloading logs (dmesg):"
dmesg | tail -n 5

# Exit with the test result code
exit $TEST_RESULT