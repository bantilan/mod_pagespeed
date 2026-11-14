- **Failed script-variable evaluation no longer leaks option copies or fails
  the request.** `ps_determine_options` frees its per-request copies on
  failure, and the resource handler serves the request without optimization
  (`NGX_DECLINED`) instead of returning an error.
