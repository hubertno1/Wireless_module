#!/bin/bash
echo "================ Start Analysis ================"

CURRENT_PATH=$(pwd)
MAIN_PATH="$CURRENT_PATH/main"

# TscanCode Analysis
echo "1. Start TscanCode Analysis..."
cd /home/hubert/TscanCode/release/linux/TscanCodeV2.14.2395.linux && ./tscancode "$MAIN_PATH" 2>"$CURRENT_PATH/tscan_result.txt"
if [ $? -eq 0 ]; then
    echo "TscanCode Analysis completed."
else
    echo "TscanCode Analysis failed!"
fi

# Return to project directory
cd "$CURRENT_PATH"

# Cppcheck Analysis
echo -e "\n2. Start Cppcheck Analysis..."
# -I: Include path for headers
# --enable=all: Enable all checks
# -i: Exclude directory
cppcheck --enable=all \
         -I "$MAIN_PATH" \
         -I "$CURRENT_PATH/esp-idf/components" \
         --suppress=missingInclude \
         --error-exitcode=1 \
         "$MAIN_PATH" 2>cppcheck_result.txt

if [ $? -eq 0 ]; then
    echo "Cppcheck Analysis completed."
else
    echo "Cppcheck Analysis found issues!"
fi

echo -e "\nAnalysis results saved to:"
echo "- TscanCode: tscan_result.txt"
echo "- Cppcheck: cppcheck_result.txt"
echo "================ Analysis Complete ================"