- **`prioritize_lcp_images` now prefers learned LCP evidence over document
  position.** When the LCP beacon has identified the hero image, that image
  takes the single preload slot wherever it appears, so a leading logo no
  longer steals the preload while the real hero is lazy-loaded. Learned
  images past the hero slot are never lazy-loaded, and evidence that matches
  nothing leaves the first image alone instead of guess-preloading it.
  Hidden images (including whitespace-tolerant inline hiding and hidden
  ancestors) are skipped, and author `fetchpriority="high"` choices are
  never lazy-loaded. Without beacon data the first-image heuristic is kept.
