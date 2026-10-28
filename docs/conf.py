"""Sphinx configuration for the generated Talos reference."""

import os

project = "talos"
extensions = ["myst_parser"]
source_suffix = {".md": "markdown", ".rst": "restructuredtext"}
root_doc = "index"
exclude_patterns = ["manifest.json"]
myst_heading_anchors = 4

html_theme = "furo"
html_title = "talos"
html_show_sourcelink = False
html_copy_source = False
html_show_copyright = False
html_use_index = False
html_static_path = ["_static"]
html_css_files = ["brand.css"]
templates_path = ["_templates"]
html_sidebars = {"**": [
    "sidebar/brand.html", "sidebar/search.html", "sidebar/scroll-start.html",
    "sidebar/navigation.html", "sidebar/links.html", "sidebar/scroll-end.html",
]}
html_theme_options = {
    "light_css_variables": {
        "color-brand-primary": "#7654b5",
        "color-brand-content": "#7654b5",
    },
    "dark_css_variables": {
        "color-brand-primary": "#bd93f9",
        "color-brand-content": "#bd93f9",
    },
    "source_repository": "https://github.com/Kuree/talos/",
    "source_branch": "main",
    "source_directory": "docs/",
    "top_of_page_buttons": [],
}
html_context = {
    "playground_url": os.environ.get("TALOS_PLAYGROUND_URL", "https://playground.talos-sv.org/"),
}
