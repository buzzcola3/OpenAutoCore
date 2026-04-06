
# OpenAutoCore

An Android Auto head unit implementation for Linux, built on a lightweight callback-driven architecture with no async frameworks.

![Architecture](doc/architecture.png)

See [doc/Architecture.md](doc/Architecture.md) for detailed documentation on each layer.

## Features

- USB (AOAP) and wireless (Bluetooth + WiFi) device connectivity
- Video projection (H.264/VP9) up to 1080p@60
- Audio playback — media, system, guidance, and telephony channels
- Audio input for voice commands (microphone capture)
- Touchscreen and key input
- Bluetooth pairing
- Automatic USB hotplug detection
- IPC transport to frontend via shared memory

## Building

**Build system:** [Bazel](https://bazel.build/) with Bzlmod

```bash
bazel build -c dbg //:openautocore
```

### System dependencies

Ensure the following are installed:

```bash
sudo apt-get install -y \
    g++ \
    libssl-dev \
    libusb-1.0-0-dev \
    libell-dev \
    qtbase5-dev \
    qttools5-dev \
    qtmultimedia5-dev \
    libprotobuf-dev \
    protobuf-compiler
```

Remaining dependencies (Boost, nlohmann_json, googletest, etc.) are managed by Bazel via `MODULE.bazel`.

## Platform

- Linux (amd64, arm64)

## License

GNU GPLv3

*Android Auto is a registered trademark of Google Inc.*

## Author

**Samuel Betak** ([buzzcola3](https://github.com/buzzcola3))

### Thanks

This project builds on the work of many contributors. Special thanks to:

- **Michal Szwaj** (f1x.studio) — original OpenAuto and aasdk author
- **Simon Dean** (CubeOne) — major contributions and modernization
- **Daniel Herr**, **Huan Truong**, **Marc Hillesheim**, **Matthew Hilton**, **Parker Reed**, **Sean Gibson** — patches and improvements

## Disclaimer

**This software is not certified by Google Inc. It is created for R&D purposes and may not work as expected by the original authors. Do not use while driving. You use this software at your own risk.**
