# PlatformIO Build and Test Guidance

- Inspect `platformio.ini` for exact environments, framework, board, flags, libraries, and scripts.
- Common commands: `pio run -e <env>` to build; `pio test -e <test-env>` when tests are configured.
- Never assume a `native` environment exists; use only environments actually configured.
- Do not upload firmware (`pio run -t upload`) or filesystems (`pio run -t uploadfs`) without explicit approval.
- Do not silently upgrade libraries, board platforms, or frameworks.
- Preserve project-specific build environment isolation (e.g., Windows vs WSL build directories).
- Report build failures and warnings; distinguish successful compilation from on-device validation.
