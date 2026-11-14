- **Inline scripts that call the support namespace check it first.** The
  dedup-images, defer-iframe, delay-images, and lazyload emitters now guard
  their calls with `if(window.pagespeed){...}`, so pages where the support
  script is blocked log no `ReferenceError`.
