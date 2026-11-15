- **LCP beacon now captures unstamped, background-image, and text LCP.**
  The beacon used to report only LCP images carrying PageSpeed's
  `data-pagespeed-url-hash` stamp, so unrewritten `<img>` LCP, CSS
  background-image LCP, and text LCP were all silent: the server could not
  tell them apart from "no data yet" and kept firing the first-image
  heuristic preload. The beacon now reports the LCP entry URL with an
  element kind (`lcpk=img|bg`, `lcpu=<url>`) when no stamp exists, and an
  explicit text marker (`lcpk=text`) for text LCP. The server stores the
  last observation per page, counts kind-only beacons toward learning
  availability, and `prioritize_lcp_images` matches unstamped heroes by
  loaded URL while leaving the first image alone when LCP names no `<img>`.
  The shipped compiled beacon scripts were regenerated from sources.
