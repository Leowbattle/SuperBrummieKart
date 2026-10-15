#!/usr/bin/env python3
"""Install the devkitPro Wii toolchain into a user-owned prefix, without root.

devkitPro's official installer (devkitpro-pacman) always installs into
/opt/devkitpro and therefore needs root. This script does the same job without
root: it downloads the official package databases, resolves the dependency
graph, verifies the SHA256 of every package and unpacks them into a prefix you
own (by default ~/devkitpro).

Usage:
    scripts/install-devkitpro-wii.py [--prefix DIR] [--jobs N] [--force]
"""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import io
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.error
import urllib.request

USER_AGENT = (
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/141.0.0.0 Safari/537.36"
)

PACKAGE_BASE = "https://pkg.devkitpro.org/packages/"
LINUX_BASE = PACKAGE_BASE + "linux/{arch}/"
LINUX_DB = LINUX_BASE + "dkp-linux.db"
LIBS_DB = PACKAGE_BASE + "dkp-libs.db"

# Everything the Wii build needs. Dependencies are pulled in automatically, so
# this list only has to name the tools and libraries we use directly.
ROOT_PACKAGES = [
    # Host toolchain
    "devkitppc-gcc",
    "devkitppc-binutils",
    "devkitppc-newlib",
    "devkitppc-crtls",
    "devkitppc-rules",
    "general-tools",
    "gamecube-tools",
    "dkp-toolchain-vars",
    "devkitppc-cmake",
    "wii-cmake",
    "wii-pkg-config",
    # Wii libraries
    "libogc",
    "libfat-ogc",
    "wii-opengx",
    "wii-sdl2",
    "wii-sdl2_image",
    "wii-sdl2_ttf",
    "ppc-libpng",
    "ppc-freetype",
    "ppc-zlib",
    "ppc-bzip2",
]

ARCH_ALIASES = {
    "amd64": "x86_64",
    "x86_64": "x86_64",
    "arm64": "aarch64",
    "aarch64": "aarch64",
}


def log(message: str) -> None:
    print(message, flush=True)


def fetch(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=120) as response:
        return response.read()


def fetch_retry(url: str, attempts: int = 4) -> bytes:
    last_error: Exception | None = None
    for attempt in range(1, attempts + 1):
        try:
            return fetch(url)
        except (urllib.error.URLError, TimeoutError) as error:
            last_error = error
            log(f"  retrying {url} ({attempt}/{attempts}): {error}")
    raise RuntimeError(f"unable to download {url}: {last_error}")


def parse_desc(text: str) -> dict[str, str]:
    """Parse a pacman `desc` file into a {SECTION: value} mapping."""
    fields: dict[str, str] = {}
    key: str | None = None
    chunks: list[str] = []
    for line in text.splitlines():
        if line.startswith("%") and line.endswith("%"):
            if key is not None:
                fields[key] = "\n".join(chunks).strip()
            key = line.strip("%")
            chunks = []
        elif line.strip():
            chunks.append(line.strip())
    if key is not None:
        fields[key] = "\n".join(chunks).strip()
    return fields


def load_database(url: str, base: str) -> dict[str, dict[str, str]]:
    """Download a pacman database and index it by package name.

    `base` is the URL directory the packages of this database are served from.
    """
    log(f"Fetching {url}")
    with tarfile.open(fileobj=io.BytesIO(fetch_retry(url)), mode="r:gz") as tar:
        packages = {}
        for member in tar.getmembers():
            if member.name.endswith("/desc"):
                extracted = tar.extractfile(member)
                if extracted is None:
                    continue
                fields = parse_desc(extracted.read().decode("utf-8", "replace"))
                fields["BASE"] = base
                name = fields.get("NAME")
                if name:
                    packages[name] = fields
    log(f"  {len(packages)} packages")
    return packages


def dependency_names(fields: dict[str, str]) -> list[str]:
    raw = fields.get("DEPENDS", "")
    names = []
    for entry in raw.split():
        # Strip version constraints such as "libogc>=3.0.0" or "zlib=1.2".
        name = entry.split(">=")[0].split("<=")[0].split("=")[0].strip()
        if name:
            names.append(name)
    return names


def resolve(packages: dict[str, dict[str, str]], roots: list[str]) -> list[str]:
    """Return the transitive closure of `roots`, in dependency-first order."""
    ordered: list[str] = []
    seen: set[str] = set()
    missing: set[str] = set()

    def visit(name: str) -> None:
        if name in seen:
            return
        seen.add(name)
        fields = packages.get(name)
        if fields is None:
            missing.add(name)
            return
        for dep in dependency_names(fields):
            visit(dep)
        ordered.append(name)

    for root in roots:
        visit(root)
    if missing:
        log(f"Warning: not in the devkitPro repositories: {', '.join(sorted(missing))}")
    return ordered


def sha256_of(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def download_package(fields: dict[str, str], directory: str) -> str:
    filename = fields["FILENAME"]
    path = os.path.join(directory, filename)
    if os.path.exists(path) and sha256_of(path) == fields.get("SHA256SUM"):
        return path
    with tempfile.NamedTemporaryFile(dir=directory, delete=False) as handle:
        handle.write(fetch_retry(fields["BASE"] + filename))
        temp_path = handle.name
    os.replace(temp_path, path)
    return path


def extract_package(path: str, prefix: str) -> None:
    # Packages store everything under opt/devkitpro/, so stripping two path
    # components drops the files straight into the prefix.
    subprocess.run(
        [
            "tar",
            "--zstd",
            "--extract",
            "--file",
            path,
            "--directory",
            prefix,
            "--strip-components=2",
            "--no-same-owner",
            "--warning=no-unknown-keyword",
        ],
        check=True,
    )


def relocate_symlinks(prefix: str) -> int:
    """Point symlinks that hardcode /opt/devkitpro at this prefix instead."""
    rewritten = 0
    for root, dirnames, filenames in os.walk(prefix):
        for name in sorted(dirnames + filenames):
            link = os.path.join(root, name)
            if not os.path.islink(link):
                continue
            target = os.readlink(link)
            if not target.startswith("/opt/devkitpro"):
                continue
            replacement = os.path.join(
                prefix, os.path.relpath(target, "/opt/devkitpro")
            )
            os.remove(link)
            os.symlink(os.path.relpath(replacement, root), link)
            rewritten += 1
    return rewritten


def relocate_texts(prefix: str) -> int:
    """Rewrite /opt/devkitpro inside scripts, pkg-config files and CMake files.

    devkitPro's packages are built for /opt/devkitpro, so they hardcode that
    path. Rewriting it is what makes the toolchain relocatable.
    """
    rewritten = 0
    for root, dirnames, filenames in os.walk(prefix):
        dirnames[:] = [d for d in dirnames if d not in (".git", "pkgcache")]
        for name in filenames:
            path = os.path.join(root, name)
            if os.path.islink(path):
                continue
            try:
                if os.path.getsize(path) > 4 << 20:
                    continue
                with open(path, "rb") as handle:
                    data = handle.read()
            except OSError:
                continue
            if b"/opt/devkitpro" not in data or b"\0" in data[:8192]:
                continue
            with open(path, "wb") as handle:
                handle.write(data.replace(b"/opt/devkitpro", prefix.encode()))
            rewritten += 1
    return rewritten


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--prefix",
        default=os.environ.get("DEVKITPRO", os.path.expanduser("~/devkitpro")),
        help="where to install (default: $DEVKITPRO or ~/devkitpro)",
    )
    parser.add_argument("--jobs", type=int, default=4, help="parallel downloads")
    parser.add_argument(
        "--force", action="store_true", help="reinstall already installed packages"
    )
    parser.add_argument(
        "packages", nargs="*", help="extra packages to install on top of the defaults"
    )
    args = parser.parse_args()

    prefix = os.path.abspath(os.path.expanduser(args.prefix))
    arch = ARCH_ALIASES.get(platform.machine().lower())
    if arch is None:
        log(f"Unsupported architecture: {platform.machine()}")
        return 1
    for tool in ("tar", "zstd"):
        if shutil.which(tool) is None:
            log(f"Missing required tool: {tool}")
            return 1

    packages = load_database(LIBS_DB, PACKAGE_BASE)
    packages.update(load_database(LINUX_DB.format(arch=arch), LINUX_BASE.format(arch=arch)))

    installed_file = os.path.join(prefix, ".installed-packages")
    installed: set[str] = set()
    if os.path.exists(installed_file) and not args.force:
        with open(installed_file) as handle:
            installed = {line.strip() for line in handle if line.strip()}

    roots = ROOT_PACKAGES + args.packages
    todo = [
        packages[name]
        for name in resolve(packages, roots)
        if packages[name]["FILENAME"] not in installed
    ]

    log(f"Installing {len(todo)} packages into {prefix}")
    if not todo:
        log("Everything is already installed")
    os.makedirs(prefix, exist_ok=True)

    with tempfile.TemporaryDirectory() as staging:
        with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            futures = {
                pool.submit(download_package, fields, staging): fields
                for fields in todo
            }
            for future in concurrent.futures.as_completed(futures):
                fields = futures[future]
                path = future.result()
                actual = sha256_of(path)
                expected = fields.get("SHA256SUM")
                if expected and actual != expected:
                    raise RuntimeError(f"Checksum mismatch for {fields['FILENAME']}")
                extract_package(path, prefix)
                installed.add(fields["FILENAME"])
                log(f"  installed {fields['NAME']} {fields.get('VERSION', '')}")

    with open(installed_file, "w") as handle:
        handle.write("\n".join(sorted(installed)) + "\n")

    links = relocate_symlinks(prefix)
    texts = relocate_texts(prefix)
    if links or texts:
        log(f"Relocated {links} symlinks and {texts} text files to {prefix}")

    log("")
    log(f"devkitPro Wii toolchain ready in {prefix}")
    log(f"  export DEVKITPRO={prefix}")
    log(f"  export DEVKITPPC={prefix}/devkitPPC")
    log(f"  export PATH={prefix}/devkitPPC/bin:{prefix}/tools/bin:$PATH")
    return 0


if __name__ == "__main__":
    sys.exit(main())
