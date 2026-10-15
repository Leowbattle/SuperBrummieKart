#!/usr/bin/env python3
"""Make Dolphin usable for automated (unattended) runs of a .dol/.elf.

Dolphin's defaults are hostile to unattended runs:

* ``Interface.ConfirmStop`` defaults to ``True``, so every emulation that ends
  (including one we terminate) pops a modal "Are you sure you want to stop
  emulation?" dialog that has to be clicked before Dolphin exits.
* ``Interface.UsePanicHandlers`` defaults to ``True``, so any emulator panic
  shows a modal alert box and the emulator just sits there until dismissed.
* ``Interface.PauseOnFocusLost`` makes the emulator pause as soon as the window
  loses focus, which happens immediately when a script launches it.
* Dolphin only writes a log file when explicitly asked, so a failed run leaves
  no evidence behind at all.

This script flips those settings in the *user's* Dolphin configuration, so the
settings also apply when the user launches Dolphin normally. Snap-packaged
Dolphin keeps its config under ``~/snap/<snap-name>/common/.config/dolphin-emu``
(the snap runs with HOME pointing at the snap's ``common`` directory), which is
why the config directory is auto-detected rather than hardcoded.

Usage:
    scripts/configure-dolphin-automation.py                 # auto-detect dirs
    scripts/configure-dolphin-automation.py --print-only     # show what it'd do
    scripts/configure-dolphin-automation.py --user-dir build/dolphin-user
"""

from __future__ import annotations

import argparse
import configparser
import io
import os
import sys
from pathlib import Path

DOLPHIN_SETTINGS = {
    "Interface": {
        "ConfirmStop": "False",  # no modal dialog when emulation stops
        "UsePanicHandlers": "False",  # no modal dialog on emulator panics
        "AbortOnPanicAlert": "False",  # keep emulating after a panic (it is logged)
        "PauseOnFocusLost": "False",  # do not pause when the window is unfocused
    },
    "Core": {
        "PauseOnPanic": "False",
    },
}

# Types worth having in dolphin.log when a homebrew program misbehaves. OSREPORT
# carries everything libogc's printf() (and SYS_Report) emits, which is the only
# way to see a Wii program's console output under Dolphin.
LOGGER_TYPES = (
    "BOOT",
    "CONSOLE",
    "CORE",
    "MASTER",
    "OSREPORT",
    "PowerPC",
    "VI",
    "Video",
)
LOGGER_OPTIONS = {
    "WriteToConsole": "True",
    "WriteToFile": "True",
    "WriteToWindow": "True",
    # 1 = notices only, 4 = include everything up to informational messages.
    "Verbosity": "4",
}


def read_ini(path: Path) -> configparser.ConfigParser:
    parser = configparser.ConfigParser(interpolation=None)
    parser.optionxform = str  # Dolphin keys are case sensitive
    if path.is_file():
        parser.read(path, encoding="utf-8")
    return parser


def apply_settings(parser: configparser.ConfigParser, settings: dict[str, dict[str, str]]) -> bool:
    changed = False
    for section, options in settings.items():
        if not parser.has_section(section):
            parser.add_section(section)
        for key, value in options.items():
            if parser.get(section, key, fallback=None) != value:
                parser.set(section, key, value)
                changed = True
    return changed


def write_ini(path: Path, parser: configparser.ConfigParser) -> None:
    buffer = io.StringIO()
    parser.write(buffer)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(buffer.getvalue(), encoding="utf-8")


def find_config_dirs(explicit: list[str], snap_names: list[str]) -> list[Path]:
    """Return the config directories Dolphin would read for the current user."""
    candidates: list[Path] = []
    home = Path.home()
    # Snap-confined Dolphin runs with HOME=<snap dir>/common.
    for snap_name in snap_names:
        snap_root = home / "snap" / snap_name
        if not snap_root.is_dir():
            continue
        # Only touch directories the emulator already uses, so we never create a
        # config Dolphin would ignore.
        for base in sorted(snap_root.iterdir()):
            config_dir = base / ".config" / "dolphin-emu"
            if config_dir.is_dir():
                candidates.append(config_dir)
    # Non-snap installs.
    for base in (home / ".config", home / ".local" / "share"):
        config_dir = base / "dolphin-emu"
        if config_dir.is_dir():
            candidates.append(config_dir)
    # Explicit `dolphin -u <dir>` style user directories.
    for item in explicit:
        candidates.append(Path(item).expanduser().resolve())

    seen: list[Path] = []
    for candidate in candidates:
        if candidate not in seen:
            seen.append(candidate)
    return seen


def config_files(config_dir: Path) -> tuple[Path, Path]:
    """Locate Dolphin.ini/Logger.ini for a directory in either layout."""
    if (config_dir / "Config" / "Dolphin.ini").is_file() or (config_dir / "Config").is_dir():
        # `dolphin -u <dir>` layout.
        return config_dir / "Config" / "Dolphin.ini", config_dir / "Config" / "Logger.ini"
    # XDG layout: the directory *is* the config directory.
    return config_dir / "Dolphin.ini", config_dir / "Logger.ini"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--user-dir", action="append", default=[], metavar="DIR",
                        help="a `dolphin -u DIR` user directory to configure (repeatable)")
    parser.add_argument("--snap-name", action="append", default=[], metavar="NAME",
                        help="snap package name(s) to look under ~/snap (default: dolphin-emu, dolphin-fresh)")
    parser.add_argument("--no-logging", action="store_true",
                        help="only disable the blocking dialogs, leave logging settings alone")
    parser.add_argument("--print-only", action="store_true", help="report what would change, write nothing")
    args = parser.parse_args()

    snap_names = args.snap_name or ["dolphin-emu", "dolphin-fresh"]
    config_dirs = find_config_dirs(args.user_dir, snap_names)
    if not config_dirs:
        print("No Dolphin config directory found; launch Dolphin once or pass --user-dir.", file=sys.stderr)
        return 1

    for config_dir in config_dirs:
        dolphin_ini, logger_ini = config_files(config_dir)
        print(f"{config_dir}")
        if args.print_only:
            print(f"  would edit: {dolphin_ini}")
            if not args.no_logging:
                print(f"  would edit: {logger_ini}")
            continue

        dolphin = read_ini(dolphin_ini)
        if apply_settings(dolphin, DOLPHIN_SETTINGS):
            write_ini(dolphin_ini, dolphin)
            print(f"  updated {dolphin_ini}")
        else:
            print(f"  already non-interactive: {dolphin_ini}")

        if args.no_logging:
            continue
        logger = read_ini(logger_ini)
        changed = apply_settings(logger, {"Options": LOGGER_OPTIONS})
        changed |= apply_settings(logger, {"Logs": {name: "True" for name in LOGGER_TYPES}})
        if changed:
            write_ini(logger_ini, logger)
            print(f"  updated {logger_ini} (log file: {config_dir.parent / '..'} -> see Logs/)")
        else:
            print(f"  logging already enabled: {logger_ini}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
