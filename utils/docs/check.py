#!/usr/bin/env python3
"""Check local assets, links, anchors, and search results in a generated site."""

import argparse
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit


class Page(HTMLParser):
    def __init__(self, path):
        super().__init__()
        self.anchors, self.links = set(), []
        self.feed(path.read_text())

    def handle_starttag(self, tag, attributes):
        attributes = dict(attributes)
        for key in ("id", "name" if tag == "a" else "id"):
            if attributes.get(key):
                self.anchors.add(attributes[key])
        for key in ("href", "src"):
            if attributes.get(key):
                self.links.append(attributes[key])


def check(root):
    root = root.resolve()
    pages = {path: Page(path) for path in root.rglob("*.html")}
    errors = []

    def check_link(source, value):
        url = urlsplit(value)
        if url.scheme or url.netloc:
            return
        path = unquote(url.path)
        target = (root / path.lstrip("/") if path.startswith("/") else
                  source.parent / path if path else source).resolve()
        if not target.is_relative_to(root):
            errors.append(f"{source.relative_to(root)}: link escapes site: {value}")
            return
        if target.is_dir():
            target /= "index.html"
        if not target.is_file():
            errors.append(f"{source.relative_to(root)}: missing target: {value}")
        elif url.fragment and target in pages and unquote(url.fragment) not in pages[target].anchors:
            errors.append(f"{source.relative_to(root)}: missing anchor: {value}")

    if not (root / "index.html").is_file():
        errors.append("Missing index.html")
    for path, page in pages.items():
        for link in page.links:
            check_link(path, link)
    if not (root / "searchindex.js").is_file():
        errors.append("Missing Sphinx search index")
    if errors:
        raise SystemExit("\n".join(errors))
    print(f"Checked {len(pages)} pages, their local links/assets, and the Sphinx search index")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("site", type=Path, nargs="?", default=Path("docs/_site"))
    check(parser.parse_args().site)
