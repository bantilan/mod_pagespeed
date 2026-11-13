# Explicit Polyfill Removal

The optional `strip_legacy_polyfills` filter must not decide that JavaScript is
unnecessary from its filename. A bundle named `polyfills.min.js` may contain
required fallback code or application initialization.

## Configuration And Authorization

The filter remains disabled by default, including under `RewriteLevel All`.
For WordPress or other generated HTML, approve exact filenames in Nginx without
editing script tags. Use one quoted filename per directive, and repeat the
directive for multiple names:

```nginx
pagespeed EnableFilters strip_legacy_polyfills;
pagespeed StripLegacyPolyfillFilename "wp-polyfill.min.js";
pagespeed StripLegacyPolyfillFilename "polyfill.min.js";
pagespeed StripLegacyPolyfillFilename "polyfills.min.js";
```

No filenames are approved by default. Configuring the list does not enable the
filter by itself. Only approve a bundle after the application owner verifies
that it is redundant for **every client supported by that application**.

Matching uses the exact, case-sensitive final HTTP(S) URL path component.
Query strings and fragments are ignored. Absolute, protocol-relative and
relative URLs are supported. Matching is not based on the hostname or on text
inside a query string. The list is additive across inherited scopes, and
duplicate entries do not change the behavior or cache identity.

For the approved filename `wp-polyfill.min.js`:

| Script source | Removed, if otherwise eligible? |
| --- | --- |
| `/wp-includes/vendor/wp-polyfill.min.js?ver=123` | Yes |
| `https://cdn.example.com/wp-polyfill.min.js` | Yes |
| `/prefix-wp-polyfill.min.js` | No |
| `/wp-polyfill.min.js.backup` | No |
| `/app.js?name=wp-polyfill.min.js` | No |
| `/WP-polyfill.min.js` | No |

This approval applies across hosts and versions. A WordPress/plugin update can
change bundle contents without changing the filename: review the approval when
upgrading. Filenames must be 1-255 ASCII letters, digits, dots, underscores or
hyphens (not `.` or `..`). Paths, URLs, wildcards, percent escapes, whitespace
and comma-separated lists are rejected. Use repeatable quoted directives, not
one quoted list. Existing URL escapes are not decoded into approved filenames.

When source HTML is under your control, an author marker remains an alternative
approval method:

```html
<script src="/verified-redundant-compatibility.js"
        data-pagespeed-remove-polyfill="true"></script>
```

The enabled filter and **either** the filename allowlist **or** the explicit
marker are required. Removal is
limited to external classic JavaScript. Inline scripts, modules, import maps,
speculation rules, other data/script types, `nomodule` fallback scripts, and
scripts marked `data-pagespeed-no-transform` are preserved even when marked.
Empty/whitespace-only sources are preserved. Only `true` grants marker approval;
the marker value comparison is case-insensitive. A false marker does not revoke
filename approval: use `data-pagespeed-no-transform` to protect a listed script.

This is author approval, not browser feature detection or an automatic audit of
bundle contents. Do not mark a bundle that supplies APIs still needed by older
browsers. A regular script filename can be approved; a familiar polyfill name
without approval is never sufficient. Removing a script can break dependent
code regardless of its size or network origin.

## Migration And Caching

Previously, enabling the filter removed matching URLs without per-script
approval. Those scripts now remain unless explicitly marked or allowlisted. This intentional
safety change may increase delivered JavaScript compared with the old policy.
Verify application functionality rather than treating the extra bytes as a bug.

The options signature includes a policy-version token only when this filter is
enabled, preventing reuse of internal cached HTML from the old policy. Other
filter configurations without this directive retain their prior identity.
The sorted/deduplicated filename list also participates in the options signature,
so changing approval cannot reuse incompatible cached HTML. Honor existing downstream
cache invalidation/deployment procedures if an external cache stores HTML.

Disable the filter to preserve every script, including marked ones:

```nginx
pagespeed DisableFilters strip_legacy_polyfills;
```

No new request logging, disk writes, client-side loaders or User-Agent variation
is introduced. This change protects correctness; it does not claim a speedup.
