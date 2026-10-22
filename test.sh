#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
mkdir -p _local/tests
python3 -m unittest discover -s tests -p 'test_*.py' -v
if [[ ${1:-} == --rack ]]; then
    rack_sdk="$(cd "${2:?Supply the Rack SDK directory}" && pwd)"
    "${CXX:-c++}" -std=c++17 -I"$rack_sdk/include" -I"$rack_sdk/dep/include" \
        -Ithird_party/bearfield-core/include tests/test_modules.cpp -L"$rack_sdk" -lRack \
        -o _local/tests/modules
    PATH="$rack_sdk:$PATH" DYLD_LIBRARY_PATH="$rack_sdk${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}" \
        LD_LIBRARY_PATH="$rack_sdk${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ./_local/tests/modules
elif [[ $# != 0 ]]; then
    echo 'Usage: ./test.sh [--rack /absolute/path/to/Rack-SDK]' >&2
    exit 2
fi
read -r -a jansson_flags <<< "$(pkg-config --cflags --libs jansson)"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Ithird_party/bearfield-core/include \
    tests/test_protocol.cpp "${jansson_flags[@]}" -o _local/tests/protocol
./_local/tests/protocol
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Ithird_party/bearfield-core/include \
    tests/test_output.cpp -o _local/tests/output
./_local/tests/output
echo 'Bearamin collection tests passed'
