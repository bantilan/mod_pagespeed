- **Inline CSS decisions now respect the document kind.** `inline_css`,
  `prioritize_critical_css` and inline `<style>` minification apply the narrow
  closing-style-tag check only on HTML pages; XHTML and unknown document types
  keep the conservative check for any `<`, so stylesheets with `@property`
  syntax descriptors such as `"<length>"` or range media queries such as
  `@media (width < 600px)` stay external there instead of breaking XML
  well-formedness. Serialized CSS is re-checked before it is written into the
  page, and metadata-cache keys carry the document kind so an HTML inline
  decision is never reused for an XHTML page.
