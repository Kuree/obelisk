import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock

spec = importlib.util.spec_from_file_location("talos_upload", Path(__file__).resolve().parents[2] / "utils/cloudflare/upload.py")
upload = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = upload
spec.loader.exec_module(upload)


class UploadTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.playground, self.docs = self.root / "playground", self.root / "docs"
        for directory in (self.playground, self.docs):
            directory.mkdir()
            (directory / "index.html").write_text("<html>Talos</html>")

    def collect(self):
        return upload.collect(self.playground, self.docs, "release-123")

    def test_collects_binary_assets_with_correct_mime_and_hashes(self):
        (self.playground / "talos.wasm").write_bytes(b"\0asm")
        (self.playground / "libtalos_rt.a").write_bytes(b"!<arch>\n")
        (self.docs / "_static").mkdir()
        (self.docs / "_static/theme.js").write_text("const a = 1;")
        (self.docs / ".doctrees").mkdir()
        (self.docs / ".doctrees/index.doctree").write_bytes(b"private build cache")
        assets = self.collect()
        self.assertEqual(len(assets), 5)
        by_name = {asset.source.name: asset for asset in assets}
        self.assertEqual(by_name["talos.wasm"].content_type, "application/wasm")
        self.assertEqual(by_name["libtalos_rt.a"].content_type, "application/octet-stream")
        self.assertTrue(by_name["theme.js"].content_type.startswith("text/javascript"))
        self.assertEqual(by_name["talos.wasm"].sha256, upload.hashlib.sha256(b"\0asm").hexdigest())
        self.assertTrue(all(asset.key.startswith("releases/release-123/") for asset in assets))

    def test_landing_assets_are_uploaded_with_the_playground(self):
        for name, content in (("landing.html", "<html>Landing</html>"),
                              ("landing.css", "body {}")):
            (self.playground / name).write_text(content)
        assets = {asset.source.name: asset for asset in self.collect()}
        self.assertEqual(assets["landing.html"].key, "releases/release-123/playground/landing.html")
        self.assertEqual(assets["landing.css"].content_type, "text/css; charset=utf-8")

    def test_rejects_symlinks_and_bad_release_names(self):
        for release in ("../other", "/absolute", "", "unpublished", "a/b"):
            with self.subTest(release=release), self.assertRaises(ValueError):
                upload.collect(self.playground, self.docs, release)
        (self.docs / "leak").symlink_to(self.playground / "index.html")
        with self.assertRaisesRegex(ValueError, "symlink"):
            self.collect()

    def test_requires_both_built_sites(self):
        (self.docs / "index.html").unlink()
        with self.assertRaisesRegex(ValueError, "missing index.html"):
            self.collect()

    def test_completion_manifest_is_written_after_every_asset(self):
        client = Mock()
        assets = self.collect()
        upload.upload(client, "sites", assets, "release-123", workers=1)
        self.assertEqual(client.upload_file.call_count, len(assets))
        self.assertEqual(client.mock_calls[-1][0], "put_object")
        manifest = client.put_object.call_args.kwargs
        self.assertEqual(manifest["Key"], "releases/release-123/manifest.json")
        self.assertEqual(upload.json.loads(manifest["Body"])["release"], "release-123")
        self.assertEqual(len(upload.json.loads(manifest["Body"])["files"]), len(assets))

    def test_failed_upload_does_not_publish_a_completion_manifest(self):
        client = Mock()
        client.upload_file.side_effect = RuntimeError("R2 rejected upload")
        with self.assertRaisesRegex(RuntimeError, "R2 rejected"):
            upload.upload(client, "sites", self.collect(), "release-123", workers=1)
        client.put_object.assert_not_called()


if __name__ == "__main__":
    unittest.main()
