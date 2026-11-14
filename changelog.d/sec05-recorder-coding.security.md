- **The in-place recorder refuses content codings it cannot undo.** Bodies in
  `br`, `zstd`, or stacked codings are no longer recorded (recording them
  would store encoded bytes as the identity form); already-refused recordings
  are not misreported as truncated.
