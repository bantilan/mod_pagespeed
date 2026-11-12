# mod_pagespeed
![logo](https://www.gstatic.com/images/branding/product/2x/pagespeed_32dp.png)

|CI|Status|
|---|---|
|Travis|[![Build Status](https://travis-ci.org/apache/incubator-pagespeed-mod.svg?branch=master)](https://travis-ci.org/apache/incubator-pagespeed-mod)|

`mod_pagespeed` is an open-source Apache module created by Google to help Make the Web Faster by rewriting web pages to reduce latency and bandwidth.

## Fork Mission

This fork exists to rebuild and revive mod_pagespeed for modern environments.
The upstream/original project has seen long periods without active maintenance,
while many production stacks still benefit from PageSpeed's optimization model.

Primary objective of this fork:
- Keep PageSpeed usable and buildable on modern toolchains.
- Continue feature updates for current web performance needs.
- Preserve backward compatibility where practical.

### Added in this fork

- AVIF image conversion support (`convert_to_avif`) for modern image delivery.
- LCP-oriented image prioritization filter to improve discoverability and fetch
  priority for above-the-fold/LCP candidates.
- Optional HTML rewrite-output caching with AVIF/WebP/original browser-variant
  isolation, beacon-aware invalidation, and per-request cache diagnostics.
- Updated Google Analytics insertion support:
  - Supports modern Google tag IDs (`GT-...`) and GA4 measurement IDs (`G-...`)
    via `gtag.js`.
  - Preserves legacy Universal Analytics behavior for `UA-...` IDs.

### Usage

Nginx example:

```nginx
pagespeed on;
pagespeed FileCachePath /var/cache/ngx_pagespeed;

# AVIF conversion
pagespeed EnableFilters convert_to_avif;

# Prioritize likely LCP images
pagespeed EnableFilters prioritize_lcp_images;

# GA insertion (choose one ID format)
pagespeed EnableFilters insert_ga;
pagespeed AnalyticsID GT-XXXXXXX;   # modern Google tag
# pagespeed AnalyticsID G-XXXXXXX;  # GA4 measurement ID
# pagespeed AnalyticsID UA-XXXXXXX-Y; # legacy Universal Analytics
```

Quick checks:

```bash
# Verify AVIF rewrite path is generated in HTML.
curl -s http://localhost/ | grep -i avif

# Verify rewritten image response is AVIF.
curl -I -H 'Accept: image/avif,image/webp,*/*' \
  'http://localhost/path/to/image.jpg.pagespeed.ic....avif'
```

### Optional HTML Cache (Nginx)

Cache completed, optimized HTML in PageSpeed's existing metadata cache to skip
HTML parsing and filter processing on subsequent hits. This is a rewrite-output
cache: the origin/static handler is still checked on every request for status,
authorization, cache policy, and validators. Both caching and diagnostics are
disabled by default.

```nginx
pagespeed HtmlCache on;               # Default: off
pagespeed HtmlCacheTtlSec 300;         # Default: 300 seconds; maximum lifetime
pagespeed ShowCacheTimestamp on;      # Default: off; optional diagnostics
```

Only successful GET responses with usable public cache freshness are eligible.
The configured TTL cannot extend the origin's cache lifetime. For an explicitly
public static blog, for example, the existing Nginx location can declare:

```nginx
location /blog/ {
    expires 5m;
}
```

Do not force public caching on personalized pages. Cookies, authorization,
request cache-control directives, private/no-store/no-cache responses, and
Set-Cookie responses bypass the HTML cache. Bodies larger than 1 MiB are not
stored.

User-Agent, Accept, client hints, and other original request headers participate
in the cache key, keeping AVIF, WebP-only, and original-format HTML separate.
Existing image detection and conversion behavior is preserved; no `<picture>`
elements or wrappers are introduced.

When critical-image beaconing is active, storage waits for three valid beacons.
New successful critical-image/LCP evidence selects a fresh HTML entry. Sampled
beacon/PerformanceObserver responses and rewrites with unfinished resource work
are not stored, so an early rewrite or sampled beacon nonce is not frozen into
cached HTML.

With diagnostics enabled, a comment is appended as the final response content,
after asynchronous rewriting and any closing `</html>` tag:

```html
<!-- PageSpeed Cache: HIT | Created: 2026-11-12 05:30:00 UTC | Age: 120s -->
```

Creation time is preserved across hits; status and age are generated per request,
not stored in the cached HTML. `MISS` indicates a new cache publication attempt;
`BYPASS` reports `Created: N/A | Age: N/A` when no entry is used or stored.
Comments are never added to non-HTML, HEAD, or 304 responses. Normal Nginx
compression applies to the final body and comment.

For browser/CDN safety, HTML handled with either option enabled is delivered with
`Cache-Control: private, no-store`; do not override that policy downstream. The
internal HTML cache uses the original origin cache policy independently.

After rebuilding the complete PSOL and ngx_pagespeed module with matching
headers, check the configuration and make repeated requests with identical
headers:

```bash
sudo nginx -t && sudo systemctl reload nginx
for attempt in 1 2 3; do
  curl -sS --compressed http://localhost/blog/ | grep -o '<!-- PageSpeed Cache:.*-->'
  sleep 1
done
```

Expect `MISS` followed by `HIT` once the response is eligible and beacon/resource
learning has settled. See [HTML cache documentation](doc/html_cache.md) for
invalidation, limitations, and integration-test commands.

The PageSpeed admin dashboard shows HTML-cache hit/miss/bypass counters, hit
rate, publication attempts, and cached HTML bytes served. Open
`/pagespeed_admin/html_cache` for recent publication history with URL search,
device/capability filters, variant IDs, sizes, creation times, and expiry.
Like Images, history requires a nonzero `pagespeed MessageBufferSize` and shows
up to 100 recent distinct variants retained in the bounded shared message
buffer. It is not a live inventory of every backend entry; expired, purged or
evicted entries may still appear as history. HTML bodies and raw request headers
are not recorded.

### Optional Historical Archive

Preserve the existing shared message buffer asynchronously without adding
rewrite-event logging. In Nginx's `http` block, alongside FileCachePath:

```nginx
pagespeed HistoryArchive on;              # Default off
pagespeed HistoryArchiveInterval 300;
pagespeed HistoryArchiveRetentionDays 7;
pagespeed HistoryArchiveMaxSizeMb 100;
```

A nonzero `MessageBufferSize` and one common FileCachePath are required.
Snapshots use a single elected writer and store each existing record once under
`<FileCachePath>/history/`, independently of normal resource-cache cleanup.
Message History, Images and HTML Cache gain historical UTC date filtering and
pagination. JavaScript/CSS pages show existing diagnostics only, not complete
per-resource rewrite histories. The dashboard reports archival health and gaps;
StatisticsLogger, Console and Graphs are unchanged.

Periodic snapshots can miss overwritten records or an unsaved restart tail;
these gaps are reported rather than hidden. There is some background CPU,
memory and disk overhead. See [archive design, limits and benchmarks](doc/history_archive.md).
A full PSOL/module rebuild and restart are required.

mod_pagespeed releases are available as [precompiled linux packages](https://modpagespeed.com/doc/download) or as [source](https://modpagespeed.com/doc/build_mod_pagespeed_from_source). (See [Release Notes](https://modpagespeed.com/doc/release_notes) for information about bugs fixed)

mod_pagespeed is an open-source Apache module which automatically applies web performance best practices to pages, and associated assets (CSS, JavaScript, images) without requiring that you modify your existing content or workflow.

mod_pagespeed is built on PageSpeed Optimization Libraries, deployed across 100,000+ web-sites, and provided by popular hosting and CDN providers such as DreamHost, GoDaddy, EdgeCast, and others. There are 40+ available optimizations filters, which include:

- Image optimization, compression, and resizing
- CSS & JavaScript concatenation, minification, and inlining
- Cache extension, domain sharding, and domain rewriting
- Deferred loading of JavaScript and image resources
- and many others...

[![Demo](http://img.youtube.com/vi/8moGR2qf994/0.jpg)](http://www.youtube.com/watch?v=8moGR2qf994)

|  Try it 	|   [modpagespeed.com](https://modpagespeed.com)	|
|---	|---	|
| Slack | https://the-asf.slack.com/archives/CJTG9RH9U |
| Read about it  |https://developers.google.com/speed/pagespeed/module   |
| Download it  | https://modpagespeed.com/doc/download  |
| Check announcements  |https://groups.google.com/group/mod-pagespeed-announce   |
| Discuss it  | https://groups.google.com/group/mod-pagespeed-discuss  |
|FAQ   | https://modpagespeed.com/doc/faq  |


Curious to learn more about mod_pagespeed? Check out our GDL episode below, which covers the history of the project, an architectural overview of how mod_pagespeed works under the hood, and a number of operational tips and best practices for deploying mod_pagespeed.

[![GDL Episode](http://img.youtube.com/vi/6uCAdQSHhmA/0.jpg)](http://www.youtube.com/watch?v=6uCAdQSHhmA)
