- **Hero preloads now match the final responsive resource.** The
  `prioritize_lcp_images` preload names the as-observed (post-rewrite) URL,
  mirrors responsive selection via `imagesrcset`/`imagesizes` (including
  single-source `media`/`type` gating), and copies the image's `crossorigin`
  mode so the preload and the image load share one cache entry. Preloads
  are skipped when the author already preloaded the hero and when
  viewport-dependent multi-source selection cannot be expressed in one hint.
  Real-browser network traces show exactly one hero download.
