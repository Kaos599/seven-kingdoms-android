# Third-party components

Archives are kept in `third_party/_downloads/` (git-ignored). Extracted trees sit beside them with the top-level folder stripped.

| Component | Version | Licence | Source URL | Archive | sha256 | Extracted to |
|---|---|---|---|---|---|---|
| Seven Kingdoms: Ancient Adversaries (7kaa) | 2.15.7 | GPL | https://sourceforge.net/projects/skfans/files/7KAA%202.15.7/7kaa-2.15.7.tar.gz/download | `7kaa-2.15.7.tar.gz` (MD5 435a063a8d84905b757be942d93232a5, verified) | 2a9833ffe5fa7558857d4f8ba39cad1dccefadb01d298350ed8f954c75b6a6ae | `7kaa/` |
| 7kaa music soundtrack | 2.15 | Not GPL (personal use only, see COPYING-Music.txt) | https://www.7kfans.com/downloads/7kaa-music-2.15.zip | `7kaa-music-2.15.zip` | b6bdcd2ca4c7f78b45b831d70f30e3c472646e84dd8ca21e1f7128f80fcf0d32 | `7kaa-music/` (MUSIC/*.WAV) |
| SDL2 | 2.32.10 (latest 2.x release, release-2.32.10) | zlib | https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz | `SDL2-2.32.10.tar.gz` | 5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165 | `SDL2/` |
| enet | 1.3.18 (tag v1.3.18) | MIT | https://github.com/lsalzman/enet/archive/refs/tags/v1.3.18.tar.gz | `enet-1.3.18.tar.gz` | 28603c895f9ed24a846478180ee72c7376b39b4bb1287b73877e5eae7d96b0dd | `enet/` |
| openal-soft | 1.25.2 (tag 1.25.2, published 2026-05-12) | LGPL-2.0 | https://github.com/kcat/openal-soft/archive/refs/tags/1.25.2.tar.gz | `openal-soft-1.25.2.tar.gz` | fb27e5839aa11f0e5b9d33756965291fad5d6909ab928ea1f796f4a1a6877894 | `openal-soft/` |

Notes:
- SDL2 was chosen as the latest release-2.x.y tag from the GitHub API listing (2.32.10 is the highest 2.x tag; SDL3 excluded).
- The 7kaa source tarball also bundles its own copies of enet, openal, curl and imgfun under `7kaa/src/`. These are the versions shipped with 7kaa 2.15.7, not the upstream tags above.
