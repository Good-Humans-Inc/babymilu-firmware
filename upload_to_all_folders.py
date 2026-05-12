#!/usr/bin/env python3
"""Upload one file to every folder under a GCS prefix.

Usage:
  python3 upload_to_all_folders.py --source /absolute/path/to/startup.wav
  python3 upload_to_all_folders.py --source ./startup.wav --name startup.wav --bucket milu-public --prefix device_bin --dry-run
  python3 upload_to_all_folders.py --source ./startup.wav --overwrite

Requirements:
  - gcloud installed and logged in (`gcloud auth login`)
  - account permission: storage.objects.create on the target bucket
"""

import argparse
import json
import mimetypes
import os
import subprocess
from urllib.parse import quote
from urllib.request import Request, urlopen
from urllib.error import HTTPError


def get_access_token() -> str:
    """Get ADC/GCloud access token for Authorization header."""
    return subprocess.check_output(["gcloud", "auth", "print-access-token"], text=True).strip()


def request_json(url: str, headers: dict) -> tuple[int, dict]:
    req = Request(url, headers=headers, method="GET")
    with urlopen(req, timeout=120) as resp:
        status = getattr(resp, "status", 200)
        payload = json.loads(resp.read().decode("utf-8")) if resp.length else {}
        return status, payload


def list_folder_prefixes(bucket: str, base_prefix: str, headers: dict) -> list[str]:
    prefixes: list[str] = []
    page_token = None
    # Normalize prefix to have trailing slash, because folders are virtual objects with delimiter logic
    if not base_prefix.endswith("/"):
        base_prefix += "/"

    while True:
        url = (
            f"https://storage.googleapis.com/storage/v1/b/{quote(bucket)}/o"
            f"?fields=prefixes,nextPageToken"
            f"&prefix={quote(base_prefix, safe='')}"
            f"&delimiter=%2F"
            f"&maxResults=1000"
        )
        if page_token:
            url += f"&pageToken={quote(page_token, safe='')}"

        _, data = request_json(url, headers)
        prefixes.extend(data.get("prefixes", []))

        page_token = data.get("nextPageToken")
        if not page_token:
            break

    return prefixes


def upload_to_object(
    bucket: str,
    object_name: str,
    data: bytes,
    content_type: str,
    headers: dict,
    overwrite: bool = False,
) -> str:
    quoted_object = quote(object_name, safe="")
    # ifGenerationMatch=0 means create only if object does not exist.
    upload_url = (
        f"https://storage.googleapis.com/upload/storage/v1/b/{quote(bucket)}/o"
        f"?uploadType=media&name={quoted_object}"
    )
    if not overwrite:
        upload_url += "&ifGenerationMatch=0"

    req = Request(
        upload_url,
        data=data,
        method="POST",
        headers={
            **headers,
            "Content-Type": content_type,
        },
    )

    try:
        with urlopen(req, timeout=300) as resp:
            if resp.status in (200, 201):
                return "uploaded"
            return f"unexpected:{resp.status}"
    except HTTPError as err:
        # 412 Precondition Failed => object exists and overwrite=False
        if err.code == 412:
            return "exists"
        if err.code == 404:
            return "not-found"
        raise


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--bucket", default="milu-public", help="GCS bucket name")
    p.add_argument("--prefix", default="device_bin", help="Top-level prefix to scan")
    p.add_argument("--source", required=True, help="Local file to upload")
    p.add_argument("--name", default="startup.wav", help="File name under each folder")
    p.add_argument("--overwrite", action="store_true", help="Overwrite if already exists")
    p.add_argument("--dry-run", action="store_true", help="Show what would be uploaded")
    return p.parse_args()


def main() -> None:
    args = parse_args()

    if not os.path.isfile(args.source):
        raise SystemExit(f"source file not found: {args.source}")

    with open(args.source, "rb") as f:
        file_bytes = f.read()

    mime_type = mimetypes.guess_type(args.source)[0] or "application/octet-stream"

    token = get_access_token()
    headers = {
        "Authorization": f"Bearer {token}",
        "Accept": "application/json",
    }

    print(f"Scanning folders under gs://{args.bucket}/{args.prefix} ...")
    folders = list_folder_prefixes(args.bucket, args.prefix, headers)
    print(f"Found {len(folders)} folder(s).")

    if not folders:
        print("No folders found; nothing to do.")
        return

    uploaded = skipped = failed = 0

    for folder in folders:
        target = f"{folder}{args.name}" if folder.endswith("/") else f"{folder}/{args.name}"

        if args.dry_run:
            print(f"DRY-RUN: gs://{args.bucket}/{target}")
            continue

        result = upload_to_object(
            bucket=args.bucket,
            object_name=target,
            data=file_bytes,
            content_type=mime_type,
            headers=headers,
            overwrite=args.overwrite,
        )

        if result == "uploaded":
            uploaded += 1
            print(f"UPLOADED: gs://{args.bucket}/{target}")
        elif result == "exists":
            skipped += 1
            print(f"SKIPPED: exists -> gs://{args.bucket}/{target}")
        else:
            failed += 1
            print(f"FAILED: gs://{args.bucket}/{target} ({result})")

    if not args.dry_run:
        print(f"Done. uploaded={uploaded}, skipped={skipped}, failed={failed}")


if __name__ == "__main__":
    main()
