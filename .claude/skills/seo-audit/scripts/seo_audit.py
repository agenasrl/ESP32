#!/usr/bin/env python3
"""Audit on-page SEO across a static website export (a .zip of crawled HTML pages).

Usage:
    python3 seo_audit.py <path-to-site.zip> [output-report.md]

No network access is used or required — everything is read from the zip.
"""
import html.parser
import os
import re
import statistics
import sys
import tempfile
import zipfile
from collections import defaultdict
from urllib.parse import urljoin, urlsplit

HTML_EXTS = (".html", ".htm")


class PageParser(html.parser.HTMLParser):
    """Pulls the SEO-relevant bits out of one HTML document."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.title = None
        self.html_lang = None
        self.meta = {}          # name/property (lowercased) -> content
        self.canonical = None
        self.headings = defaultdict(list)   # 'h1' -> [text, ...]
        self.images_missing_alt = []        # list of src
        self.links = []                     # list of href
        self.jsonld_types = []
        self.word_count = 0

        self._in_title = False
        self._in_heading = None
        self._in_script_ldjson = False
        self._skip_text_depth = 0   # inside <script> or <style>
        self._buffer = []

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        tag = tag.lower()

        if tag == "html" and "lang" in attrs:
            self.html_lang = attrs.get("lang")

        elif tag == "title":
            self._in_title = True
            self._buffer = []

        elif tag == "meta":
            key = (attrs.get("name") or attrs.get("property") or "").strip().lower()
            content = attrs.get("content", "")
            if key:
                self.meta[key] = content

        elif tag == "link":
            rel = (attrs.get("rel") or "").strip().lower()
            if rel == "canonical":
                self.canonical = attrs.get("href")

        elif tag in ("h1", "h2", "h3"):
            self._in_heading = tag
            self._buffer = []

        elif tag == "img":
            alt = attrs.get("alt")
            if alt is None or alt.strip() == "":
                self.images_missing_alt.append(attrs.get("src", "(no src)"))

        elif tag == "a":
            href = attrs.get("href")
            if href:
                self.links.append(href)

        elif tag == "script":
            script_type = (attrs.get("type") or "").strip().lower()
            if script_type == "application/ld+json":
                self._in_script_ldjson = True
                self._buffer = []
            self._skip_text_depth += 1

        elif tag == "style":
            self._skip_text_depth += 1

    def handle_endtag(self, tag):
        tag = tag.lower()
        if tag == "title" and self._in_title:
            self.title = "".join(self._buffer).strip()
            self._in_title = False

        elif tag in ("h1", "h2", "h3") and self._in_heading == tag:
            text = "".join(self._buffer).strip()
            if text:
                self.headings[tag].append(text)
            self._in_heading = None

        elif tag == "script":
            if self._in_script_ldjson:
                raw = "".join(self._buffer)
                for m in re.finditer(r'"@type"\s*:\s*"([^"]+)"', raw):
                    self.jsonld_types.append(m.group(1))
                self._in_script_ldjson = False
            self._skip_text_depth = max(0, self._skip_text_depth - 1)

        elif tag == "style":
            self._skip_text_depth = max(0, self._skip_text_depth - 1)

    def handle_data(self, data):
        if self._in_title or self._in_heading or self._in_script_ldjson:
            self._buffer.append(data)
        elif self._skip_text_depth == 0:
            words = data.split()
            self.word_count += len(words)


def find_files(root, names):
    """Case-insensitive search for files by basename anywhere under root."""
    wanted = {n.lower() for n in names}
    hits = []
    for dirpath, _dirs, files in os.walk(root):
        for f in files:
            if f.lower() in wanted:
                hits.append(os.path.join(dirpath, f))
    return hits


def parse_sitemap(path):
    try:
        text = open(path, "r", encoding="utf-8", errors="replace").read()
    except OSError:
        return []
    return re.findall(r"<loc>\s*([^<\s]+)\s*</loc>", text, re.IGNORECASE)


def parse_robots(path):
    try:
        return open(path, "r", encoding="utf-8", errors="replace").read()
    except OSError:
        return ""


def rel_url(root, path):
    return os.path.relpath(path, root).replace(os.sep, "/")


def resolve_internal_link(root, page_rel_path, href):
    """Return the zip-relative path a same-site link points to, or None if external/unresolvable."""
    href = href.strip()
    if not href or href.startswith(("#", "mailto:", "tel:", "javascript:")):
        return None
    split = urlsplit(href)
    if split.scheme or split.netloc:
        return None  # absolute URL -> treat as external; we don't know the real domain
    page_dir = os.path.dirname(page_rel_path)
    joined = os.path.normpath(os.path.join(page_dir, split.path)) if split.path else page_rel_path
    joined = joined.replace(os.sep, "/")
    return joined


def audit(zip_path, report_path):
    with tempfile.TemporaryDirectory(prefix="seo-audit-") as tmp:
        with zipfile.ZipFile(zip_path) as zf:
            zf.extractall(tmp)

        html_files = []
        for dirpath, _dirs, files in os.walk(tmp):
            for f in files:
                if f.lower().endswith(HTML_EXTS):
                    html_files.append(os.path.join(dirpath, f))
        html_files.sort()

        if not html_files:
            print(f"No .html/.htm files found inside {zip_path}", file=sys.stderr)
            sys.exit(1)

        all_files_rel = set()
        for dirpath, _dirs, files in os.walk(tmp):
            for f in files:
                all_files_rel.add(rel_url(tmp, os.path.join(dirpath, f)))

        pages = {}
        inbound_links = defaultdict(set)  # target_rel -> set(source_rel)
        for path in html_files:
            rel = rel_url(tmp, path)
            with open(path, "r", encoding="utf-8", errors="replace") as fh:
                raw = fh.read()
            parser = PageParser()
            try:
                parser.feed(raw)
            except Exception as e:
                print(f"Warning: failed to parse {rel}: {e}", file=sys.stderr)
            pages[rel] = parser

            for href in parser.links:
                target = resolve_internal_link(tmp, rel, href)
                if target:
                    inbound_links[target].add(rel)

        # robots.txt / sitemap.xml
        sitemap_hits = find_files(tmp, ["sitemap.xml"])
        robots_hits = find_files(tmp, ["robots.txt"])
        sitemap_urls = []
        for s in sitemap_hits:
            sitemap_urls.extend(parse_sitemap(s))
        robots_text = parse_robots(robots_hits[0]) if robots_hits else ""

        # Cross-page aggregation
        by_title = defaultdict(list)
        by_description = defaultdict(list)
        for rel, p in pages.items():
            if p.title:
                by_title[p.title].append(rel)
            desc = p.meta.get("description")
            if desc:
                by_description[desc].append(rel)

        sitemap_paths_guess = set()
        for u in sitemap_urls:
            path = urlsplit(u).path.lstrip("/")
            sitemap_paths_guess.add(path)

        issues = []  # (severity, text)
        page_rows = []

        for rel in sorted(pages):
            p = pages[rel]
            page_issues = []

            if not p.title:
                page_issues.append("missing <title>")
            elif len(p.title) > 60:
                page_issues.append(f"title too long ({len(p.title)} chars)")

            desc = p.meta.get("description")
            if not desc:
                page_issues.append("missing meta description")
            elif len(desc) > 160:
                page_issues.append(f"meta description too long ({len(desc)} chars)")

            h1s = p.headings.get("h1", [])
            if len(h1s) == 0:
                page_issues.append("no H1")
            elif len(h1s) > 1:
                page_issues.append(f"{len(h1s)} H1 tags (should be 1)")

            if not p.canonical:
                page_issues.append("no canonical link")

            if not p.html_lang:
                page_issues.append("no lang attribute on <html>")

            if "viewport" not in p.meta:
                page_issues.append("no viewport meta tag")

            if p.images_missing_alt:
                page_issues.append(f"{len(p.images_missing_alt)} image(s) missing alt text")

            if p.word_count < 300:
                page_issues.append(f"thin content ({p.word_count} words)")

            has_og = any(k.startswith("og:") for k in p.meta)
            has_twitter = any(k.startswith("twitter:") for k in p.meta)
            if not has_og and not has_twitter:
                page_issues.append("no Open Graph / Twitter card tags")

            if not p.jsonld_types:
                page_issues.append("no JSON-LD structured data")

            if inbound_links.get(rel) is None and rel not in ("index.html",) and not rel.endswith("/index.html"):
                if sitemap_urls and rel not in sitemap_paths_guess and not any(rel in u for u in sitemap_urls):
                    page_issues.append("orphan page: no internal links found and not in sitemap")
                elif not sitemap_urls:
                    page_issues.append("orphan page: no internal links found to this page")

            page_rows.append({
                "rel": rel,
                "title": p.title or "",
                "title_len": len(p.title) if p.title else 0,
                "desc": desc or "",
                "desc_len": len(desc) if desc else 0,
                "h1_count": len(h1s),
                "words": p.word_count,
                "missing_alt": len(p.images_missing_alt),
                "issues": page_issues,
            })

            for issue in page_issues:
                severity = "critical" if issue.startswith(("missing <title>", "missing meta description", "no H1")) else \
                           "warning" if "too long" in issue or "H1 tags" in issue or "thin content" in issue or "orphan" in issue else \
                           "info"
                issues.append((severity, f"{rel}: {issue}"))

        for title, relpages in by_title.items():
            if len(relpages) > 1:
                issues.append(("warning", f"duplicate title \"{title}\" used on: {', '.join(relpages)}"))
        for desc, relpages in by_description.items():
            if len(relpages) > 1:
                issues.append(("warning", f"duplicate meta description used on: {', '.join(relpages)}"))

        # broken internal links
        broken = []
        for rel in pages:
            p = pages[rel]
            for href in p.links:
                target = resolve_internal_link(tmp, rel, href)
                if target is None:
                    continue
                candidates = {target, target + "index.html", target.rstrip("/") + "/index.html"}
                if not (candidates & all_files_rel):
                    broken.append((rel, href))
        for src, href in broken:
            issues.append(("critical", f"{src}: broken internal link -> {href}"))

        # sitemap coverage
        if sitemap_urls:
            found_paths = set(pages.keys())
            missing_from_sitemap = []
            for rel in found_paths:
                if not any(rel in u for u in sitemap_urls):
                    missing_from_sitemap.append(rel)
            if missing_from_sitemap:
                issues.append(("info", f"{len(missing_from_sitemap)} page(s) found in export but not listed in sitemap.xml: " +
                               ", ".join(sorted(missing_from_sitemap)[:20])))

        write_report(report_path, zip_path, pages, page_rows, issues, robots_text, sitemap_urls, robots_hits, sitemap_hits)


SEVERITY_ORDER = {"critical": 0, "warning": 1, "info": 2}


def write_report(report_path, zip_path, pages, page_rows, issues, robots_text, sitemap_urls, robots_hits, sitemap_hits):
    issues.sort(key=lambda t: SEVERITY_ORDER.get(t[0], 3))

    lines = []
    lines.append("# SEO Audit Report")
    lines.append("")
    lines.append(f"- **Source:** `{os.path.basename(zip_path)}`")
    lines.append(f"- **Pages analyzed:** {len(pages)}")
    lines.append(f"- **robots.txt found:** {'yes' if robots_hits else 'no'}")
    lines.append(f"- **sitemap.xml found:** {'yes (' + str(len(sitemap_urls)) + ' URLs listed)' if sitemap_hits else 'no'}")
    lines.append("")

    lines.append("## Top Issues (by severity)")
    lines.append("")
    if not issues:
        lines.append("No issues found.")
    else:
        for severity, text in issues:
            marker = {"critical": "🔴", "warning": "🟠", "info": "🔵"}.get(severity, "-")
            lines.append(f"- {marker} **{severity.upper()}** — {text}")
    lines.append("")

    lines.append("## Per-Page Summary")
    lines.append("")
    lines.append("| Page | Title (len) | Meta Description (len) | H1s | Words | Missing Alt | Issues |")
    lines.append("|---|---|---|---|---|---|---|")
    for row in page_rows:
        title_disp = f"{row['title'][:40]}{'…' if len(row['title']) > 40 else ''} ({row['title_len']})" if row["title"] else "*(missing)*"
        desc_disp = f"{row['desc'][:40]}{'…' if len(row['desc']) > 40 else ''} ({row['desc_len']})" if row["desc"] else "*(missing)*"
        lines.append(
            f"| {row['rel']} | {title_disp} | {desc_disp} | {row['h1_count']} | {row['words']} | "
            f"{row['missing_alt']} | {len(row['issues'])} |"
        )
    lines.append("")

    if robots_hits:
        lines.append("## robots.txt")
        lines.append("")
        lines.append("```")
        lines.append(robots_text.strip())
        lines.append("```")
        lines.append("")

    with open(report_path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")

    print(f"Report written to {report_path}")
    print(f"{len(issues)} issue(s) found across {len(pages)} page(s).")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    zip_path = sys.argv[1]
    report_path = sys.argv[2] if len(sys.argv) > 2 else "seo_audit_report.md"
    audit(zip_path, report_path)


if __name__ == "__main__":
    main()
