- **Fixed a cross-site scripting issue in CSS inlining on HTML pages.**
  `inline_css` and `prioritize_critical_css` again leave a stylesheet
  external, and `rewrite_css` again leaves an inline `<style>` unrewritten,
  when the CSS would hold a `<`. Update recommended.
