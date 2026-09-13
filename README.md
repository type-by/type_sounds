# type_sounds

A high-performance, lightweight hybrid desktop music and live dual-language pronunciation player built with pure C (Win32 API) and Python, featuring smooth Spotify-style UI mechanics and real-time lyric synchronization.

## Features
- **Hybrid Architecture:** Core UI and audio rendering powered by native Win32 API for zero bloat, with Python handling asynchronous lyric fetching and translation.
- **Spotify-Style UI & Animations:** Smooth vertical interpolation (`lerp`) scrolling for lyrics, clean dark-mode aesthetics, and custom typography.
- **Dual-Language Support:** Simultaneously displays original lyrics alongside your target learning language translation fetched via LRCLIB.
- **Advanced Controls:** Real-time progress scrubbing, volume management, playlist auto-scanner, and ECO mode support.

## Tech Stack
- **Language:** C (Win32 API, MCI Media Control) & Python 3
- **Libraries:** Winmm, Psapi, Msimg32
- **Data Source:** LRCLIB API

## Getting Started
1. Clone the repository:
   ```bash
   git clone [https://github.com/type-by/type_sounds.git](https://github.com/type-by/type_sounds.git)
