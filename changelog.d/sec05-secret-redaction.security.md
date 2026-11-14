- **Secret-bearing options are redacted in admin and log output.**
  `UrlSigningKey`, `XPsaBlockingRewrite`, `DownstreamCacheRebeaconingKey`,
  `RequestOptionOverride`, `StickyQueryParameters`, Apache `ProxyAuth`, and
  `FetchProxy` now render as `***` in `OptionsToString()` (the
  `/pagespeed_admin/config` page), so exposed consoles no longer leak keys and
  credentials.
