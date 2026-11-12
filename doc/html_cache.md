# Optional optimized HTML body cache

```nginx
pagespeed HtmlCache on;               # Default: off
pagespeed HtmlCacheTtlSec 300;         # Default: 300 seconds; upper bound only
pagespeed ShowCacheTimestamp on;      # Default: off; diagnostic use
```

These options apply to the ProxyFetch HTML pipeline used by ngx_pagespeed. They
cache completed, rewritten HTML entity bodies in PageSpeed's existing metadata
cache backend, under a separate namespace. No new cache directory is needed.
The disabled path performs no HTML cache lookup or body capture.

## What is cached

This is a **rewrite-output cache**, not an origin-bypassing reverse-proxy cache.
The origin/static handler is still consulted on each request for authorization,
status, cache policy and validators. A hit skips parsing and HTML filters, then
reuses the completed body. Current origin headers still go through PageSpeed's
normal response-header processing. Changed validators or security/policy headers
select a different entry. Changing a source without validators may leave a
previously cacheable representation reusable until its declared lifetime ends.

Only successful GET responses that PageSpeed considers fresh and publicly
cacheable are eligible. `HtmlCacheTtlSec` never overrides stricter origin policy.
For example, an explicitly public **static** blog location can provide freshness
with `expires 5m;`. Do not force public caching on personalized/application pages.
Responses without usable shared-cache freshness may report BYPASS even with
`HtmlCache on`.

Requests containing cookies, Authorization/Proxy-Authorization, Cache-Control,
Pragma, Range or conditional validators bypass storage/replay. Responses with
Set-Cookie, private/no-store/no-cache policy, Vary:*, non-200 status or failed
rewrites are not cached. Experiments and `prioritize_critical_css` currently
bypass this cache rather than risk freezing personalized or sampled output.
Entries larger than 1 MiB are not stored. TTL must be positive and at most
31,536,000 seconds; values outside this range cause bypass.

## Browser variants and final rewrites

The full URL, cache partition, rewrite-options signature and **all original
request headers** participate in the cache identity. In particular, User-Agent,
Accept and client hints keep AVIF, WebP-only and original-format HTML variants
separate. No new image format detection or conversion logic is introduced.
The identity is fingerprinted using a full-length HMAC-SHA1 signature; request
header contents are not stored in the cache value. Conservative header matching
can reduce hit rate when otherwise irrelevant request headers change.

When critical-image beaconing is active, HTML is not cached until the existing
learning mechanism has received at least one valid, successful beacon result.
This HTML-cache threshold does not change the general beacon-sampling schedule.
The learned image evidence
(including accepted LCP evidence) participates in the identity. A new successful
beacon therefore invalidates the corresponding HTML variant. Sampling-due
requests bypass the cache, and responses carrying sampled beacon initialization
are not stored. Rewrites that detached resource work after missing a deadline,
or whose beacon evidence changed during rendering, are also not stored. This
allows a later request to produce and cache the settled rewrite.

Bodies are stored before transport compression. Nginx's normal compression
filters compress the final body, including any diagnostic comment. HTML cache
expiration and existing URL/global invalidation checks are applied on lookup;
diagnostics never extend entry lifetimes or perform another lookup.

## Diagnostic comment

After successful asynchronous rewriting finishes, the final output is:

```html
</html><!-- PageSpeed Cache: HIT | Created: 2026-11-12 05:30:00 UTC | Age: 120s -->
```

Existing trailing content is preserved; the new comment is appended after it,
without a trailing newline. No closing tag or wrapper is synthesized for HTML
fragments. The comment is never stored in the entry. Created is the actual cache
publication time and is preserved across hits. Age is calculated per request.
Expired/regenerated entries receive a new creation time. MISS means a new entry
was published after a lookup miss; BYPASS uses `Created: N/A | Age: N/A` when no
entry was used or stored. Backend Put is best-effort, like other PageSpeed cache
writes; it does not promise an entry will survive eviction/backend failure.

No comments are added to HEAD, 304 or non-HTML responses. With diagnostics off,
caching still works without comments. With caching off and diagnostics on,
eligible rewritten HTML reports BYPASS.

Responses handled with either option enabled are delivered as
`Cache-Control: ... private, no-store` and without stale entity digests. This
prevents downstream caches from freezing per-request diagnostics or sharing
browser-specific HTML incorrectly. It does not disable the internal body cache,
whose eligibility uses the original origin policy. Do not override this delivery
policy at a CDN. Origin TTL and browser/CDN TTL are deliberately separate here.

## Admin dashboard and HTML history

The dashboard includes HTML cache hit, lookup-miss, bypassed-lookup and
publication counters, a hit rate, and bytes served from cached HTML. Served
bytes measure reuse, not compression savings. Counters use the existing
PageSpeed statistics infrastructure and reset with those statistics.

Open `/pagespeed_admin/html_cache` (or `html_cache` under your configured admin
path) to search recent HTML publications by URL or variant and filter by device
and image capability. Rows include body size, creation time, age and expiry.
Device and capability labels describe the request, not every image in the body;
the variant fingerprint represents the full cache identity, including headers.

Like Images, the list requires a nonzero `MessageBufferSize` and uses the bounded
shared message buffer. Only cache publication attempts are recorded, not each
hit, private requests, raw headers or HTML bodies. At most 100 latest distinct
variants still retained in the buffer are displayed, newest first. This is
history, not a live backend inventory: eviction, purging or changed beacon
evidence can invalidate entries before the displayed TTL. Opening a listed URL
makes a normal new browser request; it does not retrieve that exact variant's
cached body. Existing admin access controls must remain in place.

The counters are `html_cache_hits`, `html_cache_misses`, `html_cache_bypasses`,
`html_cache_inserts` and `html_cache_bytes_served`. Misses count unsuccessful
lookups even if a subsequent rewrite cannot be stored. Bypasses count skipped
lookups while HtmlCache is enabled. Inserts count best-effort publication
attempts, not guaranteed backend retention. No backend scans or additional
per-request cache reads are introduced by the admin history.

## Tests

The GYP/Bazel `HtmlPageCacheTest` cases cover creation/age, hits, expiration,
regeneration, origin validators, browser/Accept separation, private requests and
responses, Set-Cookie, defaults, compression-backed caching, 304/non-HTML output,
and settled beacon evidence. `RewriteOptionsTest` covers option registration.

Run the transport/compression/concurrency tests against a rebuilt compatible
Nginx/module pair without touching the installed service:

```bash
python3 test/integration/html_cache_nginx_test.py \
  --nginx /path/to/nginx --module /path/to/ngx_pagespeed.so -v
```

Enabling this feature requires rebuilding the complete PSOL and ngx_pagespeed
module with matching headers. Do not link an old ngx_pagespeed object compiled
against a different RewriteOptions layout.
