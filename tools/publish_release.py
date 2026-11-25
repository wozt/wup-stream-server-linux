#!/usr/bin/env python3
"""Publish one sequential four-part release for each successful main build.

The version is allocated only after the AppImage passes CI. A GitHub tag is
created through GitHub's atomic refs API, with retries on tag collisions.
Reruns reuse a tag already pointing at the same commit and repair missing
release assets rather than making a second release.

Requires GH_TOKEN, GITHUB_REPOSITORY and GITHUB_SHA (GitHub Actions).
"""
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

TAG_PATTERN = re.compile(r"^refs/tags/v1\.0\.0\.(0|[1-9][0-9]*)$")
MAX_TAG_ATTEMPTS = 15


def read_version_tags(raw: str) -> dict[int, str]:
    """Map the numeric release index to its git object SHA."""
    tags = {}
    for line in raw.splitlines():
        fields = line.split()
        if len(fields) != 2:
            continue
        match = TAG_PATTERN.fullmatch(fields[1])
        if match:
            tags[int(match.group(1))] = fields[0]
    return tags


def choose_tag(tags: dict[int, str], commit_sha: str) -> tuple[int, bool]:
    """Reuse the current commit's tag when rerunning; otherwise increment."""
    matching = [n for n, sha in tags.items() if sha == commit_sha]
    if matching:
        return min(matching), True
    return max(tags, default=-1) + 1, False


def command(*args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        args, text=True, capture_output=True, check=False,
        env=os.environ.copy(),
    )
    if check and result.returncode:
        raise RuntimeError(
            f"Command failed ({result.returncode}): {' '.join(args)}\n"
            f"{result.stderr[-3000:]}"
        )
    return result


def remote_tags() -> dict[int, str]:
    return read_version_tags(
        command("git", "ls-remote", "--tags", "origin",
                "refs/tags/v1.0.0.*").stdout
    )


def tag_exists_on_github(repo: str, tag: str) -> bool:
    return command("gh", "release", "view", tag, "--repo", repo,
                   "--json", "tagName", check=False).returncode == 0


def reserve_tag(repo: str, sha: str) -> str:
    """Only one concurrent runner can create a given Git ref successfully."""
    for attempt in range(MAX_TAG_ATTEMPTS):
        tags = remote_tags()
        index, reused = choose_tag(tags, sha)
        tag = f"v1.0.0.{index}"
        if reused:
            print(f"Reusing {tag} for this commit ({sha[:12]}).", flush=True)
            return tag

        result = command(
            "gh", "api", "-X", "POST", f"repos/{repo}/git/refs",
            "-f", f"ref=refs/tags/{tag}", "-f", f"sha={sha}",
            check=False,
        )
        if result.returncode == 0:
            print(f"Reserved new version {tag} ({sha[:12]}).", flush=True)
            return tag

        # A concurrent workflow may have allocated this version first.
        updated = remote_tags()
        if index in updated:
            if updated[index] == sha:
                return tag
            print(f"{tag} claimed by another build; retrying.", flush=True)
            time.sleep(min(1 + attempt, 5))
            continue
        raise RuntimeError(f"Cannot create {tag}: {result.stderr[-2000:]}")
    raise RuntimeError("Too many concurrent tag collisions")


def release_asset_names(repo: str, tag: str) -> set[str]:
    proc = command(
        "gh", "release", "view", tag, "--repo", repo,
        "--json", "assets", "--jq", ".assets[].name", check=False,
    )
    if proc.returncode:
        return set()
    return set(proc.stdout.splitlines())


def build_assets(original: Path, tag: str) -> tuple[Path, Path]:
    version = tag.removeprefix("v")
    output = original.with_name(f"wupstreamd-{version}-x86_64.AppImage")
    if output != original:
        shutil.copy2(original, output)
    sha = hashlib.sha256(output.read_bytes()).hexdigest()
    checksums = output.parent / "SHA256SUMS"
    checksums.write_text(f"{sha}  {output.name}\n", encoding="utf-8")
    return output, checksums


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: publish_release.py dist/wupstreamd-x86_64.AppImage")
    repo = os.environ.get("GITHUB_REPOSITORY", "")
    sha = os.environ.get("GITHUB_SHA", "")
    if not re.fullmatch(r"[\w.-]+/[\w.-]+", repo):
        raise RuntimeError("GITHUB_REPOSITORY is absent or invalid")
    if not re.fullmatch(r"[0-9a-f]{40}", sha):
        raise RuntimeError("GITHUB_SHA is absent or invalid")
    if not os.environ.get("GH_TOKEN"):
        raise RuntimeError("GH_TOKEN is required to create tags and releases")

    artifact = Path(sys.argv[1]).resolve()
    if not artifact.is_file() or artifact.stat().st_size < 1024:
        raise RuntimeError(f"Missing or empty AppImage: {artifact}")
    if artifact.open("rb").read(4) != b"\x7fELF":
        raise RuntimeError(f"Not an ELF/AppImage: {artifact}")

    tag = reserve_tag(repo, sha)
    appimage, checksums = build_assets(artifact, tag)
    want = {appimage.name, checksums.name}
    available = release_asset_names(repo, tag)

    if want.issubset(available):
        print(f"Release {tag} already contains both assets; nothing to do.")
        return
    if tag_exists_on_github(repo, tag):
        # Re-run after a partial asset upload; --clobber replaces stale copies.
        command("gh", "release", "upload", tag, str(appimage), str(checksums),
                "--clobber", "--repo", repo)
        print(f"Repaired assets of {tag}.")
    else:
        command("gh", "release", "create", tag,
                str(appimage), str(checksums),
                "--verify-tag", "--repo", repo,
                "--title", tag.removeprefix("v"),
                "--generate-notes")
        print(f"Published release {tag}.")
    print(f"AppImage: {appimage.name}; SHA256: {checksums.name}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"Release failed: {exc}", file=sys.stderr)
        sys.exit(1)
