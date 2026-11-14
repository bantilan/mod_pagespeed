- **Bound and isolate combined-resource fetches.** Fetch inputs load a few at
  a time and stop once they exceed the cacheable body size; over-long resource
  names and names decoding to zero URLs are refused; combinations stop at the
  cacheable size; and a fetch's result no longer becomes the metadata-cache
  entry that HTML rewrites read (fetch/combine isolation).
