- **LCP beaconing now works under minimal `prioritize_lcp_images` configs and
  no longer drops late LCP observations.** The critical-images beacon used
  to require an image-rewriting filter (`lazyload_images`, `inline_images`,
  `delay_images` or `resize_to_rendered_image_dimensions`) before it would
  inject its script, so a pass-through config with only
  `prioritize_lcp_images` could never learn: the filter stayed on the
  first-image heuristic forever. `prioritize_lcp_images` now triggers beacon
  insertion on its own. Separately, the beacon's one-shot window-load send
  gave up permanently when load fired before the first LCP entry was
  delivered (late first paint), while the page-hide path drained the
  observer without ever sending. The beacon now keeps observing past load
  when it has no data yet and completes the send at page hide, reporting
  the final LCP image. Load-time sends with data present are unchanged.
  The shipped compiled beacon, lazyload, responsive and critical-CSS-loader
  scripts were also regenerated from current sources; they had drifted
  since the LCP-beacon fork and copy-mode builds served beacon code with
  no LCP support at all.
