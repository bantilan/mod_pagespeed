- **Oversized gzip resources are served unmodified.** A body that inflates past
  the handled size is treated as un-extractable and skipped by rewrites such
  as cache extension, instead of having its still-compressed bytes served as
  plain content.
