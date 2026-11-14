- **Inline JS/CSS is minified unless the CSP uses hash sources.** Minification
  now proceeds under `nonce-*` and `unsafe-inline` (without `strict-dynamic`)
  policies, where rewriting the text cannot change browser acceptance, and
  still stands down when a hash source covers the exact bytes.
