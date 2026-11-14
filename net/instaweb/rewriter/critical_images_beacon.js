/*
 * Copyright 2013 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

goog.provide('pagespeed.CriticalImages');

goog.require('goog.dom');
goog.require('goog.dom.TagName');
goog.require('pagespeedutils');


/**
 * Collects LCP signal (modern path) and beacons to server.
 * @param {string} beaconUrl The URL on the server to send the beacon to.
 * @param {string} htmlUrl URL of the page the beacon is being inserted on.
 * @param {string} optionsHash The hash of the rewrite options.
 * @param {boolean} checkRenderedImageSizes Whether to send rendered dimensions.
 * @param {string} nonce The nonce sent by the server.
 * @constructor
 * @private
 */
pagespeed.CriticalImages.Beacon_ = function(
    beaconUrl, htmlUrl, optionsHash, checkRenderedImageSizes, nonce) {
  this.beaconUrl_ = beaconUrl;
  this.htmlUrl_ = htmlUrl;
  this.optionsHash_ = optionsHash;
  this.nonce_ = nonce;
  this.checkRenderedImageSizes_ = checkRenderedImageSizes;

  /** @private {string} */
  this.lcpImage_ = '';

  /** @private {?Object} */
  this.lcpObserver_ = null;

  /** @private {boolean} */
  this.beaconSent_ = false;

  this.setupLcpObserver_();
};


/**
 * Records an LCP entry if it corresponds to a rewritten image hash.
 * @param {?Object} entry
 * @private
 */
pagespeed.CriticalImages.Beacon_.prototype.trackLcpEntry_ = function(entry) {
  if (!entry || !entry.element || !entry.element.getAttribute) {
    return;
  }
  var key = entry.element.getAttribute('data-pagespeed-url-hash');
  if (key) {
    this.lcpImage_ = key;
  }
};


/**
 * Enables LCP collection using PerformanceObserver where available.
 * @private
 */
pagespeed.CriticalImages.Beacon_.prototype.setupLcpObserver_ = function() {
  if (!window.PerformanceObserver) {
    return;
  }

  var beacon = this;
  var finalize = function() {
    if (!beacon.lcpObserver_) {
      return;
    }
    if (beacon.lcpObserver_.takeRecords) {
      var records = beacon.lcpObserver_.takeRecords();
      for (var i = 0; i < records.length; ++i) {
        beacon.trackLcpEntry_(records[i]);
      }
    }
    beacon.lcpObserver_.disconnect();
    beacon.lcpObserver_ = null;
    // Complete a send deferred past window load. checkCriticalImages_ is
    // one-shot, so this is a no-op when the beacon already went out.
    beacon.checkCriticalImages_();
  };

  try {
    beacon.lcpObserver_ = new PerformanceObserver(function(list) {
      var entries = list.getEntries();
      for (var i = 0; i < entries.length; ++i) {
        beacon.trackLcpEntry_(entries[i]);
      }
    });
    beacon.lcpObserver_.observe(
        {type: 'largest-contentful-paint', buffered: true});

    pagespeedutils.addHandler(document, 'visibilitychange', function() {
      if (document.visibilityState == 'hidden') {
        finalize();
      }
    });
    pagespeedutils.addHandler(window, 'pagehide', finalize);
  } catch (e) {
    beacon.lcpObserver_ = null;
  }
};


/**
 * Builds and sends the beacon once.
 * @private
 */
pagespeed.CriticalImages.Beacon_.prototype.checkCriticalImages_ = function() {
  if (this.beaconSent_) {
    return;
  }

  if (this.lcpObserver_ && this.lcpObserver_.takeRecords) {
    var records = this.lcpObserver_.takeRecords();
    for (var i = 0; i < records.length; ++i) {
      this.trackLcpEntry_(records[i]);
    }
  }

  var data = 'oh=' + this.optionsHash_;
  if (this.nonce_) {
    data += '&n=' + this.nonce_;
  }

  var isDataAvailable = false;
  if (this.lcpImage_) {
    var lcp = '&lcp=' + encodeURIComponent(this.lcpImage_);
    if (data.length + lcp.length <= pagespeedutils.MAX_POST_SIZE) {
      data += lcp;
      isDataAvailable = true;
    }
  }

  if (this.checkRenderedImageSizes_) {
    var tmp = '&rd=' +
        encodeURIComponent(JSON.stringify(this.getImageRenderedMap()));
    if (data.length + tmp.length <= pagespeedutils.MAX_POST_SIZE) {
      data += tmp;
      isDataAvailable = true;
    }
  }

  if (!isDataAvailable && this.lcpObserver_) {
    // Window load fired before the first LCP entry was delivered (late
    // first paint). Keep observing instead of giving up: the observer
    // keeps tracking the latest LCP image and the page-hide finalize
    // completes the send.
    return;
  }
  if (this.lcpObserver_) {
    this.lcpObserver_.disconnect();
    this.lcpObserver_ = null;
  }
  this.beaconSent_ = true;

  pagespeed.CriticalImages.beaconData_ = data;
  if (isDataAvailable) {
    pagespeedutils.sendBeacon(this.beaconUrl_, this.htmlUrl_, data);
  }
};


/**
 * Triggers beacon send. Called by onload or lazyload completion path.
 * @export
 */
pagespeed.CriticalImages.checkCriticalImages = function() {
  pagespeed.CriticalImages.beaconObj_.checkCriticalImages_();
};


/**
 * Retrieves rendered image dimensions map used by resize-to-rendered flow.
 * @return {!Object.<string, {
 *     rw: number,
 *     rh: number,
 *     ow: number,
 *     oh: number}>}
 */
pagespeed.CriticalImages.Beacon_.prototype.getImageRenderedMap = function() {
  var renderedImageDimensions = {};
  var images = goog.dom.getElementsByTagName(goog.dom.TagName.IMG);
  if (images.length == 0) { return {}; }

  var img = images[0];
  if (!('naturalWidth' in img) || !('naturalHeight' in img)) { return {}; }

  for (var i = 0; img = images[i]; ++i) {
    var key = img.getAttribute('data-pagespeed-url-hash');
    if (!key) { continue; }
    if ((!(key in renderedImageDimensions) &&
             img.width > 0 && img.height > 0 &&
             img.naturalWidth > 0 && img.naturalHeight > 0) ||
        ((key in renderedImageDimensions) &&
             img.width >= renderedImageDimensions[key].rw &&
             img.height >= renderedImageDimensions[key].rh)) {
      renderedImageDimensions[key] = {
        'rw': img.width,
        'rh': img.height,
        'ow': img.naturalWidth,
        'oh': img.naturalHeight
      };
    }
  }
  return renderedImageDimensions;
};


/** @private string */
pagespeed.CriticalImages.beaconData_ = '';


/** @private Object Beacon object */
pagespeed.CriticalImages.beaconObj_;


/**
 * Gets the data sent in the beacon after pagespeed.CriticalImages.Run().
 * @return {string}
 * @export
 */
pagespeed.CriticalImages.getBeaconData = function() {
  return pagespeed.CriticalImages.beaconData_;
};


/**
 * Initializes LCP beaconing and optionally schedules send at window load.
 * @param {string} beaconUrl
 * @param {string} htmlUrl
 * @param {string} optionsHash
 * @param {boolean} sendBeaconAtOnload
 * @param {boolean} checkRenderedImageSizes
 * @param {string} nonce
 * @export
 */
pagespeed.CriticalImages.Run = function(
    beaconUrl, htmlUrl, optionsHash, sendBeaconAtOnload,
    checkRenderedImageSizes, nonce) {
  var beacon = new pagespeed.CriticalImages.Beacon_(
      beaconUrl, htmlUrl, optionsHash, checkRenderedImageSizes, nonce);
  pagespeed.CriticalImages.beaconObj_ = beacon;
  if (sendBeaconAtOnload) {
    var beaconOnload = function() {
      window.setTimeout(function() { beacon.checkCriticalImages_(); }, 0);
    };
    pagespeedutils.addHandler(window, 'load', beaconOnload);
  }
};
