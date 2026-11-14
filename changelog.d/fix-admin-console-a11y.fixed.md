- **Admin console accessibility and runtime hardening.** The image
  comparison dialog now contains Tab focus while open (Escape, backdrop
  close and focus restoration already worked). The caches, graphs and
  statistics sub-tabs expose `tablist`/`tab`/`tabpanel` semantics with
  arrow-key navigation while keeping click, hash deep-links and styling
  unchanged. Charts keep their accessible names and additionally expose
  values: bar-chart SVG text is navigable instead of flattened, and
  line charts carry visually-hidden data tables. Auto-refresh polling
  pauses while the tab is hidden and refreshes deliberately on return.
  The graphs page explains missing statistics logging instead of
  loading forever (including a no-JavaScript note), the message search
  box no longer stretches at narrow widths, and low-contrast gray text
  now meets 4.5:1. The shipped compiled admin scripts were also
  regenerated from current sources (they had drifted since 2016,
  including a previously missing `config` build).
