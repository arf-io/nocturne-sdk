#!/bin/zsh
set -eu
set -o pipefail

if (( $# != 8 )); then
    print -u2 "Usage: $0 PIN SCRATCH NATIVE_SDK WASM_SDK WASM_TOOLCHAIN NPM_CACHE WHEELHOUSE WASI_NOTICES"
    exit 2
fi

pin=$(git rev-parse "${1}^{commit}")
repo=$(git rev-parse --show-toplevel)
scratch=${2:A}
native_prefix=${3:A}
wasm_prefix=${4:A}
wasm_toolchain=${5:A}
npm_cache=${6:A}
wheelhouse=${7:A}
notices=${8:A}
source_dir="$scratch/source"
artifacts="$scratch/artifacts"

mkdir -p "$scratch"
mkdir "$source_dir" "$artifacts" "$scratch/traces"
git archive "$pin" CMakeLists.txt bindings tests/consumer/binding_replay |
    tar -xf - -C "$source_dir"
sdk_version=$(sed -En 's/^project\(Nocturne VERSION ([0-9]+\.[0-9]+\.[0-9]+) .+/\1/p' "$source_dir/CMakeLists.txt")
[[ -n "$sdk_version" ]]
python3 "$repo/tools/pack_go_module.py" "$pin" "$artifacts/go"
printf '%s\n' "$pin" > "$artifacts/SOURCE-COMMIT"
fixtures="$source_dir/tests/consumer/binding_replay"
export SOURCE_DATE_EPOCH=$(git show -s --format=%ct "$pin") PYTHONDONTWRITEBYTECODE=1

cmake -S "$fixtures" -B "$scratch/c" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$native_prefix"
cmake --build "$scratch/c" --config Release
"$scratch/c/binding_replay" > "$scratch/traces/c.trace"

(
    export NOCTURNE_SDK_PREFIX="$native_prefix" ARF_BUILD_ROOT="$scratch/rust-build"
    cd "$source_dir/bindings/rust"
    arf-build package --offline --locked
    crates=("$ARF_BUILD_ROOT"/**/package/nocturne-"$sdk_version".crate)
    (( ${#crates} == 1 ))
    cp "$crates[1]" "$artifacts/"
    mkdir -p "$scratch/rust/unpacked" "$scratch/rust/consumer/src"
    tar -xzf "$artifacts/nocturne-$sdk_version.crate" -C "$scratch/rust/unpacked"
    cmp "$scratch/rust/unpacked/nocturne-$sdk_version/LICENSE" "$native_prefix/share/licenses/nocturne/LICENSE"
    cd "$scratch/rust/consumer"
    cp "$fixtures/replay.rs" src/main.rs
    cat > Cargo.toml <<TOML
[package]
name = "nocturne-binding-replay"
version = "1.0.0"
edition = "2021"

[dependencies]
nocturne = { path = "../unpacked/nocturne-$sdk_version", features = ["posix-host", "graph", "spatial", "simulation"] }
TOML
    arf-build run --offline --quiet > "$scratch/traces/rust.trace"
)

(
    mkdir -p "$scratch/python/boot"
    cd "$scratch/python"
    cp "$fixtures/replay.py" .
    export NOCTURNE_SDK_PREFIX="$native_prefix" NOCTURNE_PYTHON_COMPONENTS=posix_host,graph,spatial,simulation
    export CFLAGS="${CFLAGS:+$CFLAGS }-g0"
    for dependency in setuptools wheel packaging; do
        build_wheel=("$wheelhouse"/${dependency}-*.whl)
        unzip -oq "$build_wheel[1]" -d boot
    done
    PYTHONPATH="$scratch/python/boot" python3 -m pip --isolated wheel --no-index \
        --no-build-isolation --find-links "$wheelhouse" --no-deps \
        --wheel-dir "$artifacts" "$source_dir/bindings/python"
    wheel=("$artifacts"/nocturne_sdk-*.whl)
    python3 -m pip --isolated install --no-index --no-compile --find-links "$wheelhouse" \
        --target "$scratch/python/installed" "$wheel[1]"
    PYTHONPATH="$scratch/python/installed" python3 -s - "$scratch/python/installed" <<'PYTHON'
from pathlib import Path
import sys
import cffi
import nocturne

prefix = Path(sys.argv[1]).resolve()
assert Path(cffi.__file__).resolve().is_relative_to(prefix)
assert Path(nocturne.__file__).resolve().is_relative_to(prefix)
PYTHON
    PYTHONPATH="$scratch/python/installed" python3 -s replay.py > "$scratch/traces/python.trace"
)

(
    pkg=github.com/arf-io/nocturne-sdk/bindings/go
    version="v$sdk_version"
    mkdir -p "$scratch/go/consumer" "$scratch/go/cache" "$scratch/go/tmp" "$scratch/go/modcache"
    proxy=$(python3 -c 'from pathlib import Path; import sys; print(Path(sys.argv[1]).as_uri())' "$artifacts/go")
    export GOPROXY="$proxy" GOSUMDB=off GONOPROXY=none GOWORK=off GOTOOLCHAIN=local CGO_ENABLED=1
    export GOCACHE="$scratch/go/cache" GOTMPDIR="$scratch/go/tmp" GOMODCACHE="$scratch/go/modcache"
    cd "$scratch/go/consumer"
    cp "$fixtures/replay.go" main.go
    go mod init example.org/nocturne-binding-replay
    go mod edit -require="$pkg@$version"
    go mod download -json "$pkg@$version" > module.json
    go run "$pkg/cmd/nocturne-config@$version" -prefix "$native_prefix" \
        -components posix_host,graph,spatial,simulation > cgo-build-flags.zsh
    source ./cgo-build-flags.zsh
    go mod verify
    go build -tags "$NOCTURNE_GO_TAGS" -o replay .
    ./replay > "$scratch/traces/go.trace"
    cp go.sum "$artifacts/GO-SUM"
)

(
    export NOCTURNE_SDK_PREFIX="$wasm_prefix" NOCTURNE_WASI_TOOLCHAIN="$wasm_toolchain"
    export NOCTURNE_WASI_NOTICE_DIR="$notices" npm_config_cache="$npm_cache"
    export ZIG_LOCAL_CACHE_DIR="$scratch/zig-cache" ZIG_GLOBAL_CACHE_DIR="$scratch/zig-global-cache"
    cd "$source_dir/bindings/typescript"
    npm ci --offline --ignore-scripts --no-audit --no-fund
    npm run build
    npm test
    npm pack --offline --pack-destination "$artifacts" --json > "$scratch/pack.json"
    mkdir "$scratch/typescript"
    cd "$scratch/typescript"
    cp "$fixtures/replay.mjs" .
    tgz=("$artifacts"/*.tgz)
    npm install --offline --ignore-scripts --no-audit --no-fund "$tgz[1]"
    node replay.mjs > "$scratch/traces/typescript.trace"
)

python3 "$fixtures/compare.py" "$scratch/traces"
(
    cd "$artifacts"
    shasum -a 256 SOURCE-COMMIT GO-SUM *.crate *.whl *.tgz go/**/*(.) > SHA256SUMS
    shasum -a 256 -c SHA256SUMS
)
