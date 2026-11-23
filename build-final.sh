#!/bin/bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "Usage: $0 /path/to/ns-3.42" >&2
  exit 1
fi

NS3_DIR="$(cd "$1" && pwd)"
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUTPUT="$PROJECT_DIR/ecn-final-bin"

includes=(
  "-I$NS3_DIR/build/include"
  "-I$NS3_DIR/src"
  "-I$PROJECT_DIR/modules/traffic-generator"
  "-I$PROJECT_DIR/modules/vbr-generator"
  "-I$PROJECT_DIR/modules/ecn-monitor"
  "-I$PROJECT_DIR/modules/ecn-controller"
  "-I$PROJECT_DIR/modules/bdp-monitor"
  "-I$PROJECT_DIR/modules/bdp-controller"
)

sources=(
  "$PROJECT_DIR/modules/traffic-generator/traffic-generator.cc"
  "$PROJECT_DIR/modules/vbr-generator/vbr-generator.cc"
  "$PROJECT_DIR/modules/ecn-monitor/ecn-monitor.cc"
  "$PROJECT_DIR/modules/ecn-controller/ecn-controller.cc"
  "$PROJECT_DIR/modules/bdp-monitor/bdp-monitor.cc"
  "$PROJECT_DIR/modules/bdp-controller/bdp-controller.cc"
  "$PROJECT_DIR/simulation/main.cc"
)

mapfile -t libraries < <(find "$NS3_DIR/build/lib" -maxdepth 1 -name 'libns3.42-*.so' -print)
if [ "${#libraries[@]}" -eq 0 ]; then
  echo "No ns-3.42 shared libraries found; build ns-3.42 first." >&2
  exit 1
fi

g++ -std=c++20 "${includes[@]}" "${sources[@]}" -o "$OUTPUT" \
  "${libraries[@]}" "-Wl,-rpath,$NS3_DIR/build/lib"
echo "Built $OUTPUT without modifying $NS3_DIR"
