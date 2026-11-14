- **Image-format selection now honors explicit WebP rejection.** A client
  that sends `Accept: image/webp;q=0` no longer receives WebP rewritten
  URLs via User-Agent matching; the `q=0` veto mirrors the existing AVIF
  behavior. Omitting the token (as navigation requests do) still allows
  User-Agent-based selection, and `*/*` never counts as format acceptance.
- **Learned LCP evidence now matches images rewritten after the beacon.**
  The beacon reports hashes of original image URLs, but
  `prioritize_lcp_images` runs post-rewrite, so once image rewriting
  kicked in the learned hero never matched: the hero lost its preload
  and was lazy-loaded. The filter now prefers the stamped original-URL
  hash, and the beacon filter keeps stamping those hashes (without the
  script) on learned renders where no new beacon is due. Tags without a
  stamp fall back to the previous current-URL comparison.
- **The external AVIF encoder is now spawned without `fork()`.** The
  encoder ran via `system()` from multithreaded rewrite workers, where
  a fork deadlock silently stalls all image rewriting (pages serve
  progressively less-optimized variants with no error). It now uses
  `posix_spawnp` with the same arguments, redirections and exit-status
  check, and logs spawn/exit status on failure.
