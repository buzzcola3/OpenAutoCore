
# OpenAutoCore

[![Contributor Covenant](https://img.shields.io/badge/Contributor%20Covenant-2.1-4baaaa.svg)](code_of_conduct.md)

### Support project

### Community
[![Join the chat at https://gitter.im/publiclab/publiclab](https://badges.gitter.im/Join%20Chat.svg)](https://gitter.im/openauto_androidauto/Lobby)

### Description
OpenAutoCore is a Headless AndroidAuto(tm) headunit, to which a Head application written in Flutter, .NET or React can be attached to using Shared Memory and gRPC,

### Supported platforms

 - Linux aarch64/x86_64

### Build
 - The build system Bazel, fully hermetic
   
 - to build x86:
    bazel build --config=amd64 //:OpenAutoCore_bin
 - to build aarch64:
    bazel build --config=arm64 //:OpenAutoCore_bin

### License
GNU GPLv3

Copyrights (c) 2025 buzzcola3 (Samuel Betak)
Copyrights (c) 2018 f1x.studio (Michal Szwaj)

*AndroidAuto is registered trademark of Google Inc.*
