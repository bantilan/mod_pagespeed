- **Stylesheets that contain a "<" are temporarily not inlined.**
  This applies to `prioritize_critical_css` and `inline_css`, for example to
  stylesheets with `@property` syntax descriptors (Tailwind CSS 4) or range
  media queries.
