# mod_pagespeed
![logo](https://www.gstatic.com/images/branding/product/2x/pagespeed_32dp.png)

**A continuation of PageSpeed for modern image delivery, browser-aware HTML
rewriting, and better optimization visibility.**

This fork builds on Google's original `mod_pagespeed` and the Apache PageSpeed
codebase. It automatically optimizes HTML and associated images, CSS, and
JavaScript at the server, without requiring application templates to be
rewritten. Its focus is keeping PageSpeed usable on modern toolchains while
extending image delivery, reducing repeated rewrite work, and making the
optimization pipeline easier to inspect.

The project retains its Apache module heritage and provides the PageSpeed
Optimization Libraries (PSOL) used by the companion
[ngx_pagespeed fork](https://github.com/bantilan/ngx_pagespeed). The Nginx
examples below require that integration; this repository alone is not an Nginx
module or a complete Nginx distribution.

## What This Fork Adds

- **Modern image delivery.** AVIF conversion through `convert_to_avif`, with
  browser-supported fallback: prefer AVIF for compatible browsers, fall back
  to WebP for WebP-only browsers, and retain the original image format when
  neither is supported. Existing image elements,
  attributes, and CSS remain intact; format negotiation does not require
  introducing `<picture>` elements or extra wrappers.
- **LCP-oriented optimization.** The `prioritize_lcp_images` filter and
  critical-image beacon feedback help identify and prioritize important images.
- **Optional HTML rewrite caching.** Reuse completed HTML rewrites to avoid
  repeated parsing and filter work. Request and browser variants remain
  isolated, while cache eligibility and invalidation account for origin policy
  and learned beacon evidence.
- **An expanded admin console.** A sidebar-based dashboard, image comparisons,
  HTML publication history, grouped message logs, and updated statistics,
  histogram, cache, console, and graph views make optimization activity easier
  to investigate.
- **Optional historical archiving.** Background snapshots preserve existing
  shared-buffer records and merge them with live history. A dedicated archive
  page reports storage, retention, snapshot health, and detected gaps.
- **Updated analytics insertion.** The `insert_ga` filter supports Google tag
  IDs (`GT-...`) and GA4 measurement IDs (`G-...`) through `gtag.js`, while
  retaining legacy Universal Analytics (`UA-...`) insertion behavior.
- **Build and profiling work.** Compatibility fixes and tools in
  [`benchmark/`](benchmark/) support investigating rewrite latency, cache
  behavior, and archival overhead on the target environment.

The existing PageSpeed filter model remains available, including image
compression and resizing, CSS/JavaScript minification and inlining, and resource
cache optimizations. Backward compatibility is preserved where practical;
new features should be enabled and tested explicitly for each deployment.

### Browser-Aware Image Fallback

When AVIF conversion and the relevant image rewrite filters are enabled, format
selection uses HTTP `Accept` together with User-Agent and browser-version
capability checks. This also supports initial HTML requests whose `Accept`
header does not explicitly advertise image formats.

| Browser capability | Preferred image delivery |
| --- | --- |
| AVIF supported | AVIF, when conversion is eligible and beneficial |
| AVIF unsupported, WebP supported | WebP fallback |
| Neither AVIF nor WebP supported | Original format, with applicable optimizations |

Unknown or partial AVIF support is handled conservatively rather than assuming
every Chromium-based browser supports it. Format selection does not guarantee
conversion for every image: content, size savings, enabled filters, and rewrite
limits still apply. Browser-dependent HTML cache variants remain isolated so an
AVIF-capable browser's cached page is not reused for an incompatible browser.

## Deployment Notes

- Build PSOL and the Nginx module with matching source and headers. Updating the
  repository does not update an installed module; rebuild and install the
  resulting artifacts before testing changes.
- HTML rewrite caching is not a full-page reverse-proxy cache. The origin or
  static handler is still consulted, and personalized or non-cacheable responses
  must not become shared HTML entries.
- Dashboard savings, rewrite counts, cached bytes served, and history records
  measure different things. In particular, HTML cache delivery bytes are not
  HTML compression savings, and diagnostic archives cannot reconstruct missing
  aggregate counters.
- Optional caching and archiving are disabled by default. Benchmark the
  enabled features under representative traffic rather than assuming a
  universal performance improvement.

## Usage

Nginx example:

```nginx
pagespeed on;
pagespeed FileCachePath /var/cache/ngx_pagespeed;

# Image optimization with AVIF and WebP fallback where supported
pagespeed EnableFilters rewrite_images,convert_to_avif,convert_jpeg_to_webp;

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

When critical-image beaconing is active, storage waits for one valid, successful
beacon result. The general beacon-sampling schedule remains unchanged.
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

The dashboard includes HTML publication activity and cached bytes served;
HTML compression savings are marked as unmeasured. Open
`/pagespeed_admin/html_cache` for HTML-cache hit/miss/bypass counters, hit rate,
publication attempts, cached HTML bytes served, and publication history with URL search,
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
When enabled, Message History, Images, HTML Cache, JavaScript, and CSS combine
the live buffer with retained archive records in their existing views, with UTC
date query filtering and older-record pagination. JavaScript/CSS show existing
diagnostics only, not complete per-resource rewrite histories. Archive health
and gaps are shown at `/pagespeed_admin/history`; the dashboard keeps its
performance overview, merges retained image records into its recent-image
section, and links to that page. Aggregate counters are separate from diagnostic
history: with `StatisticsLogging on` and a configured `LogDir`, the dashboard
and HTML Cache page can combine available earlier-generation statistics samples
with current live counters. The archive worker also triggers due statistics
samples and a final sample during graceful shutdown. Unsampled or trimmed data
cannot be recovered from publication records. Console and Graphs continue to
use StatisticsLogger's time-series data.

Periodic snapshots can miss overwritten records or an unsaved restart tail;
these gaps are reported rather than hidden. There is some background CPU,
memory and disk overhead. See [archive design, limits and benchmarks](doc/history_archive.md).
A full PSOL/module rebuild and restart are required.

## Documentation And Heritage

- [HTML rewrite cache](doc/html_cache.md): eligibility, browser isolation,
  invalidation, and testing.
- [Historical archive](doc/history_archive.md): shared-buffer snapshots,
  retention, restart recovery, and operational limits.
- [Benchmark tools and results](benchmark/): profiling and performance checks.
- [Companion ngx_pagespeed fork](https://github.com/bantilan/ngx_pagespeed):
  Nginx integration using PSOL from this project.

This fork builds on work by the original Google PageSpeed team, Apache
contributors, and the wider PageSpeed community. The original
[PageSpeed documentation](https://developers.google.com/speed/pagespeed/module)
is useful background for inherited filters and configuration. Upstream
documentation and packages do not necessarily include this fork's changes.

The project retains the Apache License 2.0; see [LICENSE](LICENSE) and
[NOTICE](NOTICE) for licensing and attribution.
