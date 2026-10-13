# 143 — Search metadata without another public page

## Tweet idea

Search engines can now understand the original iPhone emulator without adding another page to the experience: descriptive metadata, structured data, robots.txt and a one-URL sitemap keep the phone itself front and center.

## What you're seeing

This is primarily a crawler-facing change. The existing [minimal phone recording](videos/minimalist_phone.mp4) and [thumbnail](thumbnails/minimalist_phone.jpg) show the unchanged public experience. There is no separate About link or page to record.

## ELI5 technical explanation

A search engine cannot learn much from the iPhone's canvas pixels. The old page description was only “📱 iPhone (2007),” so the home page now includes a clear title and description, matching social-preview text, its canonical URL and WebSite structured data. robots.txt points crawlers to a sitemap containing only the emulator home page.

An About link and static guide were briefly deployed with the first version of this work. They were removed by request from the interface, source tree, offline package and sitemap. A regression test verifies that `about.html` does not exist and that neither the home page nor sitemap links to it.

## Why it matters

Visitors still land directly on the original iPhone with no extra site navigation, while crawlers receive an accurate explanation of what the otherwise pixel-driven page contains. Search-engine indexing and chosen snippets remain outside the project's control.

## Release

The metadata first went live in production commit `51f211b361806e0ac5d6fdab3295eb8e1e03aff6`. Removing the About surface is verified locally and awaits release.
