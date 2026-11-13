- **JavaScript rewrites preserve execution and integrity semantics.**
  `combine_javascript` no longer touches `integrity`-pinned scripts and no
  longer combines where inline scripts are forbidden (its `eval()` stubs
  would be blocked, breaking the page). `rewrite_javascript` skips
  `integrity` scripts, `inline_javascript` skips `async`/`defer` scripts
  (inlining would run them synchronously), `outline_javascript` skips
  `integrity` scripts and strips meaningless `async`/`defer` when moving
  code external, and domain rewriting leaves module and CORS-fetched
  scripts alone. The legacy minifier now fails closed on template literals
  instead of corrupting them. Modules, import maps, speculation rules and
  data blocks pass through every script filter untouched.
