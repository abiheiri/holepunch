# holepunch

A command-line utility that uses UPnP to temporarily forward an external port on your home router to a local TCP service.

## Building

Requires [libminiupnpc](https://github.com/miniupnp/miniupnp) and a C11 compiler.

### macOS

```bash
brew install miniupnpc
make
```

### Linux

```bash
sudo apt-get install libminiupnpc-dev
make
```

### Cross-compilation

```bash
# Linux aarch64
make CC=aarch64-linux-gnu-gcc

# macOS x86_64 from Apple Silicon
make CC="clang -arch x86_64"
```

## Usage

```bash
# Forward local SSH to external port 2222
punch -sport 22 -dport 2222

# Forward with UDP too
punch -sport 8080 -dport 80 -proto both

# Run in background
punch -sport 25565 -dport 25565 -background

# Stop background forward
punch -kill -dport 25565

# Show version
punch --version

# Self-update from GitHub releases
punch --update
```

## Releasing

Push a git tag to trigger the GitHub Actions release workflow:

```bash
git tag v1.0.0
git push origin v1.0.0
```

The workflow builds binaries for Linux (x86_64, aarch64) and macOS (x86_64, arm64) and creates a GitHub release automatically.
