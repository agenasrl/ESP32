---
name: seo-audit
description: Audits on-page SEO for a website from a ZIP file containing an exported/crawled copy of its pages (HTML files, optionally with robots.txt and sitemap.xml) — no live network access needed. Use this whenever the user asks for an SEO audit, SEO check, or SEO review and provides (or offers to provide) a .zip of website pages, especially in environments where fetching the live URL isn't possible (blocked egress, offline, or the site isn't deployed yet). Also use it for phrases like "audit the SEO of this export", "check titles and meta descriptions across these pages", "find duplicate titles/meta descriptions", "find pages missing alt text or H1s", or "check my sitemap/robots.txt against my pages". Do not use this for auditing a live URL directly — this skill only reads from a provided zip.
---

# SEO Audit (from a website export ZIP)

Audits on-page SEO across a static export of a website — a `.zip` containing
HTML pages, and optionally `robots.txt` / `sitemap.xml` — without needing to
fetch anything over the network. This matters whenever live fetching isn't an
option (blocked egress, a site not yet deployed, or the user simply has the
files on hand and it's faster to read them directly).

## When the user hasn't provided a zip yet

If the user wants an audit of a live site but only gave a URL, ask them to
export the site as a zip first (a crawler like `wget --mirror`, `httrack`, a
"save as" from their CMS, or a static site build output all work), since this
skill reads from files, not the network. Point out that most static site
generators and many CMSs can produce this export directly.

## Running the audit

Run the bundled script against the zip. It extracts to a temp directory,
parses every `.html`/`.htm` file with Python's standard library (no
dependencies to install), and writes a Markdown report:

```bash
python3 .claude/skills/seo-audit/scripts/seo_audit.py <path-to-site.zip> <output-report.md>
```

If the output path is omitted, it defaults to `seo_audit_report.md` in the
current directory. Prefer writing the report to the user's scratchpad or a
path they specify, not into the middle of an unrelated project's source tree.

The script checks, per page: title presence/length, meta description
presence/length, canonical tag, `lang` attribute, viewport tag, H1 count and
duplicates, images missing `alt` text, Open Graph/Twitter tags, JSON-LD
structured data, and an approximate word count (flagging thin content). Across
the whole site it also flags duplicate titles/descriptions, orphan pages (no
internal links pointing to them and not listed in the sitemap), broken
internal links, and — when `robots.txt`/`sitemap.xml` are present in the
zip — surfaces their contents and cross-checks the sitemap against the pages
actually found.

## Presenting the results

- Read the generated report and summarize the highest-severity findings
  (🔴 critical, then 🟠 warning) in your reply — don't just say "see the
  report," walk through what's actually broken and why it matters for SEO
  (e.g. a missing title/H1 affects how search engines understand the page;
  duplicate titles confuse ranking; broken internal links waste crawl budget
  and hurt UX).
- Send the full Markdown report to the user as a file so they have the
  per-page table and complete issue list to work from.
- If asked, offer to fix the underlying HTML/template issues directly, but
  only when the user has that source available in a project you can edit —
  this skill's script only reads and reports, it doesn't modify the site.

## Notes on the approach

The parser is intentionally dependency-light (only `zipfile`, `html.parser`,
and `re` from the standard library) so it runs anywhere without an install
step or network access to fetch a package. Word counts, thin-content
thresholds, and link resolution are heuristic approximations — good enough to
prioritize real issues, not a certified crawler. If a page uses heavy
client-side rendering (SPA with little server-rendered HTML), be upfront that
this static analysis will under-count its real content and links.
