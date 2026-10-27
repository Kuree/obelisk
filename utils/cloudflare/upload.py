#!/usr/bin/env python3
"""Upload both static sites to an isolated R2 release; write its manifest last."""

import argparse
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
import hashlib
import json
import mimetypes
import os
from pathlib import Path
import re


@dataclass(frozen=True)
class Asset:
    source: Path
    key: str
    size: int
    sha256: str
    content_type: str


def content_type(path):
    overrides = {
        ".a": "application/octet-stream", ".wasm": "application/wasm",
        ".js": "text/javascript; charset=utf-8", ".mjs": "text/javascript; charset=utf-8",
        ".css": "text/css; charset=utf-8", ".html": "text/html; charset=utf-8",
        ".json": "application/json", ".svg": "image/svg+xml",
    }
    return overrides.get(path.suffix.lower()) or mimetypes.guess_type(path.name)[0] or "application/octet-stream"


def collect(playground, docs, release):
    if not re.fullmatch(r"[a-zA-Z0-9][a-zA-Z0-9._-]{0,127}", release) or release == "unpublished":
        raise ValueError("Invalid release name")
    assets = []
    for name, directory in (("playground", playground), ("docs", docs)):
        directory = Path(directory).resolve()
        if not (directory / "index.html").is_file():
            raise ValueError(f"{name}: missing index.html in {directory}")
        for path in sorted(directory.rglob("*")):
            if path.is_symlink():
                raise ValueError(f"Refusing symlink: {path}")
            relative = path.relative_to(directory)
            # Sphinx build caches and local preview markers are not site assets.
            if any(part.startswith(".") for part in relative.parts) or not path.is_file():
                continue
            with path.open("rb") as stream:
                digest = hashlib.file_digest(stream, "sha256").hexdigest()
            assets.append(Asset(path, f"releases/{release}/{name}/{relative.as_posix()}",
                                path.stat().st_size, digest, content_type(path)))
    return assets


def upload(client, bucket, assets, release, workers=8):
    def put(asset):
        # upload_file uses multipart transfer for the compiler's large WASM.
        client.upload_file(str(asset.source), bucket, asset.key, ExtraArgs={
            "ContentType": asset.content_type,
            "CacheControl": "public, max-age=0, must-revalidate",
            "Metadata": {"sha256": asset.sha256},
        })

    with ThreadPoolExecutor(max_workers=workers) as executor:
        list(executor.map(put, assets))
    manifest = {"release": release, "files": [
        {"key": asset.key, "size": asset.size, "sha256": asset.sha256,
         "content_type": asset.content_type} for asset in assets
    ]}
    # An upload error prevents this completion marker and the subsequent Worker
    # deployment. Previous releases remain available for rollback.
    client.put_object(Bucket=bucket, Key=f"releases/{release}/manifest.json",
                      Body=json.dumps(manifest, sort_keys=True).encode(),
                      ContentType="application/json", CacheControl="no-store")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--playground", required=True, type=Path)
    parser.add_argument("--docs", required=True, type=Path)
    parser.add_argument("--release", required=True)
    parser.add_argument("--config", type=Path, default=Path(__file__).with_name("wrangler.json"))
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    config = json.loads(args.config.read_text())
    bucket = next(item["bucket_name"] for item in config["r2_buckets"] if item["binding"] == "SITES")
    assets = collect(args.playground, args.docs, args.release)
    total = sum(asset.size for asset in assets)
    print(f"{'Would upload' if args.dry_run else 'Uploading'} {len(assets)} files "
          f"({total / 1024**2:.1f} MiB) to {bucket}/releases/{args.release}/")
    if args.dry_run:
        return

    import boto3
    from botocore.config import Config

    required = ("CLOUDFLARE_ACCOUNT_ID", "R2_ACCESS_KEY_ID", "R2_SECRET_ACCESS_KEY")
    missing = [key for key in required if not os.environ.get(key)]
    if missing:
        raise SystemExit("Missing credentials: " + ", ".join(missing))
    account = os.environ["CLOUDFLARE_ACCOUNT_ID"]
    endpoint = os.environ.get("R2_ENDPOINT_URL") or f"https://{account}.r2.cloudflarestorage.com"
    client = boto3.client("s3", endpoint_url=endpoint, region_name="auto",
                          aws_access_key_id=os.environ["R2_ACCESS_KEY_ID"],
                          aws_secret_access_key=os.environ["R2_SECRET_ACCESS_KEY"],
                          config=Config(request_checksum_calculation="when_required",
                                        response_checksum_validation="when_required"))
    upload(client, bucket, assets, args.release)
    print("Release upload complete")


if __name__ == "__main__":
    main()
