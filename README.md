<!-- {"canonical_sha256": "f6a92805aa194e5df044548369280b50167e318798a93bd7908569a84ae137f6", "canonical_source": "engineering:Bearamin-VCV/README", "editing": "Edit the canonical Obsidian source; regenerate this copy.", "generated": true, "vault_commit": null} -->

# Bearamin for VCV Rack

Two modules for slow modulation and optical gesture control, with Bearamin as the collection name.

| Module | What it does | Hardware |
| --- | --- | --- |
| SloFO | Phase-aligned sine, triangle and square CV, with an encoder and 8×8 display | Runs entirely in Rack; based on [SloFO](https://github.com/actuallysparky/SloLFO) |
| Bearamin Lite | Six X/Y/Z CVs and Presence/Spank gates, powered by Bearamin Virtual | Requires a [Bearamin Virtual](https://github.com/actuallysparky/bearamin/tree/main/bearamin-virtual) USB sensor controller |

The physical [Bearamin Lite](https://github.com/actuallysparky/bearamin/tree/main/bearamin-lite) has its own sensor, display and CV circuitry. This Rack module presents that style of controls and outputs using the smaller Bearamin Virtual controller as its input. It reads the Virtual serial stream directly; no bridge application is needed.

## SloFO

Tap the encoder to select Minutes, Hours, Days or Months. Drag vertically or scroll to change the period: 1–60 minutes, 1–24 hours, 1–64 days in half-day steps, or 1–64 fixed 30-day months. A month is exactly 30 days.

Hold for 0.5 seconds to preview the voltage mode; keep holding to two seconds to switch between 0–5 V and −5–5 V. All three outputs share one phase. The matrix shows the edit selection, then returns to the waveform display after two seconds.

SloFO starts immediately. There is no reserve-capacitor charging animation or delay. The context menu offers time acceleration, phase selection and simulated power off/on. Power off gives zero outputs and pauses the phase; power on resumes immediately from that phase. Saved phase, period, selected unit, voltage mode and time acceleration remain supported.

## Bearamin Lite, powered by Bearamin Virtual

Connect the RP2040-Zero/VL53L7CX Bearamin Virtual controller, add **Bearamin Lite**, and choose its USB serial port in the module context menu. The depth field appears in the matrix. Range scales the six CVs; Slew smooths them. X/Y/Z have 0–5 V and −5–5 V outputs; Presence and Spank are 0/5 V gates. The panel's HOLD jack is the Spank output.

Rotation, mirroring and port selection are saved in the patch. Boot, sensor faults, disconnects and data older than 250 ms force all outputs to zero immediately. Virtual protocol v1 and the existing Bearfield processing are preserved. A fresh valid frame is required after loading a patch. Select a device only in one plugin instance, and close other serial readers before connecting it here. Virtual's separate USB MIDI interface can remain in use by a DAW.

## Install and build

Build with the official [Rack 2 SDK](https://vcvrack.com/manual/Building#Building-Rack-plugins), version 2.6.6:

```sh
make -j2 RACK_DIR=/absolute/path/to/Rack-SDK
make dist RACK_DIR=/absolute/path/to/Rack-SDK
```

The root Makefile and plugin.json are the plugin entrypoints. Artwork and the pinned Bearfield header are included; a build needs no sibling repository, firmware toolchain or KiCad. The package is `dist/Bearamin-2.1.0-<platform>.vcvplugin`. Close Rack, copy the package to its user folder's matching `plugins-<platform>` directory, then restart Rack. Search for **Bearamin** to find **SloFO** and **Bearamin Lite**.

Linux packages built on Ubuntu 22.04 require glibc 2.35 or later. Windows x64, Linux x64, Intel Mac and Apple Silicon Mac builds are covered by the build workflow.

## Existing patches and presets

The collection uses plugin slug `Bearamin`. The former plugins used `SlowLFOPrototype` and `BearaminVirtual`; Rack treats these as different plugins. Keep the old packages available for existing patches. The original source repositories and released packages remain available at the links above.

To make a separate copy of a patch or module preset for the new collection:

```sh
python3 tools/migrate_patch.py original.vcv bearamin-copy.vcv
```

Python 3 and `zstd` are required for Rack 2 compressed patches. Plain JSON patches and module presets are also supported. The tool changes only the two recognized plugin/module identifiers; it preserves module IDs, cables, positions, parameters, saved module data and archive storage files. It refuses to overwrite the original or an existing output file. Open the copy in Rack and verify it before retiring the original. Old charging-duration data is ignored by SloFO.

## Tests

With Python 3, a C++17 compiler, `pkg-config`, Jansson and `zstd` installed:

```sh
./test.sh
./test.sh --rack /absolute/path/to/Rack-SDK
python3 tools/check_package.py dist/Bearamin-2.1.0-<platform>.vcvplugin
```

On Windows, running the SDK tests also requires the matching Rack `libRack.dll` on `PATH`; the SDK itself includes only its import library. CI extracts that DLL from the official Rack Free installer without running the installer.

The SDK test exercises the actual module classes without opening a hardware port. Connected sensor operation and musical use require a live check in Rack.

## Sources and credits

The collection brings together the Rack work from [SloFO](https://github.com/actuallysparky/SloLFO) and [Bearamin Virtual](https://github.com/actuallysparky/bearamin/tree/main/bearamin-virtual). Hardware and firmware continue in those repositories. SloFO was inspired by Patience, with thanks to the wonders of slow time. The shared Bearfield processor comes from the Bearamin project. Panel artwork is derived from the corresponding SloFO and Bearamin Lite faceplates; checked-in SVGs are the build inputs.

Plugin code is GPL-3.0-or-later; Bearfield retains its MIT license. Both notices are included in each package. [Bearamin family, hardware and credits](https://github.com/actuallysparky/bearamin).
