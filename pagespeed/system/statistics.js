/*
 * Copyright 2014 Google Inc.
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

/**
 * @fileoverview Implements auto-refresh and filtering for statistics page.
 *
 * @author xqyin@google.com (XiaoQian Yin)
 */

goog.provide('pagespeed.Statistics');

goog.require('goog.array');
goog.require('goog.events');
goog.require('goog.net.EventType');
goog.require('goog.net.XhrIo');
goog.require('goog.string');


/** @typedef {{name: string, value:string}} */
pagespeed.StatsNode;



/**
 * @constructor
 * @param {goog.testing.net.XhrIo=} opt_xhr Optional mock XmlHttpRequests
 *     handler for testing.
 */
pagespeed.Statistics = function(opt_xhr) {
  /**
   * The XmlHttpRequests handler for auto-refresh. We pass a mock XhrIo
   * when testing. Default is a normal XhrIo.
   * @private {goog.net.XhrIo|goog.testing.net.XhrIo}
   */
  this.xhr_ = opt_xhr || new goog.net.XhrIo();

  /**
   * The most recent statistics list.
   * @private {Array.<pagespeed.StatsNode>}
   */
  this.psolMessages_ = [];
  var messages = document.getElementById('stat').textContent.split('\n');
  for (var i = 0; i < messages.length; ++i) {
    var tmp = messages[i].split(':');
    if (tmp.length < 2) continue;
    var node = {
      name: tmp[0].trim(),
      value: tmp[1].trim()
    };
    this.psolMessages_.push(node);
  }

  /**
   * The total number of unfiltered messages.
   * @private {number}
   */
  this.numUnfilteredMessages_ = this.psolMessages_.length;

  /**
   * The statistics list after last refresh. The type of this object is like
   * the initializer below. this.lastValue_['cache_hits'] is the value of
   * 'cache_hits' after last refresh.
   * @private {!Object}
   */
  this.lastValue_ = {'statsName': 0};

  /**
   * Heuristic for how often each variable changes. It basically
   * counts the number of changes we've noticed since the first
   * refresh. The exact value is not important, instead this is used
   * to distinguish "more active" stats from "less active" ones.
   *
   * For example, this.timesOfChange_['cache_hits'] is the number of
   * times of changes of 'cache_hits' since the first refresh.
   * @private {!Object}
   */
  this.timesOfChange_ = {'statsName': 0};
  for (var i = 0; i < this.psolMessages_.length; i++) {
    var initial = this.psolMessages_[i];
    var initialValue = Number(initial.value) || 0;
    this.lastValue_[initial.name] = initialValue;
    this.timesOfChange_[initial.name] = initialValue ? 1 : 0;
  }

  /**
   * We provide filtering functionality for users to search for certain
   * statistics like all the statistics related to caches. This option is the
   * matching pattern. Filtering is case-insensitive.
   * @private {string}
   */
  this.filter_ = '';

  /** @private {string} */
  this.activeCategory_ = 'all';

  /**
   * The option of auto-refresh. If true, the page will automatically refresh
   * itself. The default frequency is to refresh every 5 seconds.
   * @private {boolean}
   */
  this.autoRefresh_ = false;

  /**
   * If this option is false, the statistics is sorted alphabetically. When it
   * is set to true, the statistics is sorted in descending order of the
   * number of changes and secondarily alphabetic order.
   * @private {boolean}
   */
  this.sortByNumberOfChanges_ = false;

  /**
   * The flag of whether the first refresh is started. We need to call a
   * refresh when loading the page to sort statistics. Default is false,
   * which means the page hasn't started the first refresh yet.
   * @private {boolean}
   */
  this.firstRefreshStarted_ = false;

  var statElement = document.getElementById('stat');
  var wrapper = document.createElement('div');
  wrapper.className = 'pagespeed-statistics-page';
  var summary = document.createElement('div');
  summary.className = 'pagespeed-stat-summary';
  summary.id = 'pagespeed-stat-summary';
  wrapper.appendChild(summary);

  var overview = document.createElement('div');
  overview.className = 'pagespeed-stat-overview';
  overview.innerHTML =
      '<section class="pagespeed-stat-card">' +
      '<h2>Cache activity</h2>' +
      '<div class="pagespeed-stat-cache-content">' +
      '<div class="pagespeed-stat-donut" id="pagespeed-stat-donut">' +
      '<div class="pagespeed-stat-donut-center" id="pagespeed-stat-donut-center"></div>' +
      '</div><div class="pagespeed-stat-cache-legend" id="pagespeed-stat-cache-legend"></div>' +
      '</div></section>' +
      '<section class="pagespeed-stat-card">' +
      '<h2>Optimization activity</h2>' +
      '<div class="pagespeed-stat-optimization" id="pagespeed-stat-optimization"></div>' +
      '</section>';
  wrapper.appendChild(overview);

  var tablePanel = document.createElement('section');
  tablePanel.className = 'pagespeed-stat-table-panel';
  var toolbar = document.createElement('div');
  toolbar.className = 'pagespeed-stat-toolbar';
  toolbar.innerHTML =
      '<div class="pagespeed-stat-category-tabs" id="pagespeed-stat-category-tabs" role="tablist">' +
      '<button type="button" role="tab" aria-selected="false" data-category="all" class="is-active">All counters</button>' +
      '<button type="button" role="tab" aria-selected="false" data-category="cache">Cache</button>' +
      '<button type="button" role="tab" aria-selected="false" data-category="css">CSS</button>' +
      '<button type="button" role="tab" aria-selected="false" data-category="images">Images</button>' +
      '<button type="button" role="tab" aria-selected="false" data-category="fonts">Fonts</button>' +
      '<button type="button" role="tab" aria-selected="false" data-category="other">Other</button></div>' +
      '<input id="text-filter" type="search" placeholder="Search counters..." aria-label="Search counters">' +
      '<label class="pagespeed-stat-toggle">Auto refresh <input type="checkbox" id="auto-refresh"></label>' +
      '<label class="pagespeed-stat-toggle">Sort by activity <input type="checkbox" id="sort-freq"></label>';
  tablePanel.appendChild(toolbar);
  var numElement = document.createElement('div');
  numElement.id = 'num';
  numElement.className = 'pagespeed-show-number';
  tablePanel.appendChild(numElement);
  wrapper.appendChild(tablePanel);
  var statParent = statElement.parentNode;
  statParent.insertBefore(wrapper, statElement);
  statElement.className = 'pagespeed-statistics-content';
  statElement.textContent = '';
  tablePanel.appendChild(statElement);

  goog.events.listen(document.getElementById('sort-freq'), 'change',
                     goog.bind(this.toggleSorting, this));
  var categories = document.querySelectorAll(
      '#pagespeed-stat-category-tabs [data-category]');
  for (var i = 0; i < categories.length; i++) {
    goog.events.listen(categories[i], 'click', goog.bind(
        this.setCategory_, this, categories[i].getAttribute('data-category')));
  }
  this.update();
};


/**
 * Updates the option of auto-refresh.
 */
pagespeed.Statistics.prototype.toggleAutorefresh = function() {
  this.autoRefresh_ = !this.autoRefresh_;
};


/**
 * Updates the option of sort-by-number-of-changes.
 */
pagespeed.Statistics.prototype.toggleSorting = function() {
  this.sortByNumberOfChanges_ = !this.sortByNumberOfChanges_;
  this.update();
};


/**
 * Updates the option of statistics filter.
 * @param {!HTMLElement} element The HTML element of Filter.
 */
pagespeed.Statistics.prototype.setFilter = function(element) {
  this.filter_ = element.value;
  this.update();
};


/**
 * Update the text showing the number of statistics.
 * @param {number=} opt_num An optional specified number to show when
 *     filtering the messages.
 */
pagespeed.Statistics.prototype.updateMessageCount = function(opt_num) {
  // The default number of messages is the length of the array when opt_num
  // is not specified.
  var total = (opt_num != undefined) ? opt_num : this.psolMessages_.length;
  document.getElementById('num').textContent =
      'Showing ' + total.toLocaleString() + ' of ' +
      this.numUnfilteredMessages_.toLocaleString() + ' counters';
};


/**
 * Show the error message.
 */
pagespeed.Statistics.prototype.error = function() {
  goog.array.clear(this.psolMessages_);
  this.numUnfilteredMessages_ = 0;
  this.updateMessageCount();
  document.getElementById('stat').textContent =
      pagespeed.Statistics.REFRESH_ERROR_;
  this.renderSummary_();
};


/**
 * Filters and displays updated statistics.
 */
pagespeed.Statistics.prototype.update = function() {
  if (this.sortByNumberOfChanges_) {
    // Sort in descending order of the number of changes.
    this.sortByNumberOfChanges();
  } else {
    // Sort in alphabetical order.
    this.sortByAlphabet();
  }

  var messages = goog.array.clone(this.psolMessages_);
  for (var i = messages.length - 1; i >= 0; --i) {
    if ((this.filter_ && (!messages[i].name ||
        !goog.string.caseInsensitiveContains(messages[i].name,
                                             this.filter_))) ||
        !pagespeed.Statistics.matchesCategory_(
            messages[i].name, this.activeCategory_)) {
      messages.splice(i, 1);
    }
  }
  this.updateMessageCount(messages.length);
  this.updateCategoryTabs_();
  this.renderSummary_();

  /** @type {!Object<string, !Array<!pagespeed.StatsNode>>} */
  var groups = {};
  for (var i = 0; i < pagespeed.Statistics.GROUPS_.length; ++i) {
    groups[pagespeed.Statistics.GROUPS_[i].id] = [];
  }
  for (var i = 0; i < messages.length; ++i) {
    groups[pagespeed.Statistics.groupForName_(messages[i].name)].push(
        messages[i]);
  }

  var statElement = document.getElementById('stat');
  statElement.innerHTML = '';
  for (var i = 0; i < pagespeed.Statistics.GROUPS_.length; ++i) {
    var group = pagespeed.Statistics.GROUPS_[i];
    var items = groups[group.id];
    if (!items.length) continue;

    var section = document.createElement('section');
    section.className = 'pagespeed-stat-group';
    var heading = document.createElement('div');
    heading.className = 'pagespeed-stat-group-heading';
    var title = document.createElement('h2');
    title.textContent = group.title;
    var description = document.createElement('p');
    description.textContent = group.description;
    heading.appendChild(title);
    heading.appendChild(description);
    section.appendChild(heading);

    var table = document.createElement('table');
    table.className = 'pagespeed-admin-data-table pagespeed-stats-table';
    var header = table.createTHead().insertRow(0);
    var tableBody = document.createElement('tbody');
    table.appendChild(tableBody);
    var metricHeader = document.createElement('th');
    metricHeader.textContent = 'Metric';
    header.appendChild(metricHeader);
    var valueHeader = document.createElement('th');
    valueHeader.textContent = 'Current value';
    header.appendChild(valueHeader);
    var changesHeader = document.createElement('th');
    changesHeader.textContent = 'Changes';
    changesHeader.title =
        'Changes observed during auto-refresh; the count grows while ' +
        'auto-refresh is enabled.';
    header.appendChild(changesHeader);
    var subsystemHeader = document.createElement('th');
    subsystemHeader.textContent = 'Subsystem';
    header.insertBefore(subsystemHeader, changesHeader);
    var descriptionHeader = document.createElement('th');
    descriptionHeader.textContent = 'Description';
    header.insertBefore(descriptionHeader, changesHeader);
    for (var j = 0; j < items.length; ++j) {
      var row = tableBody.insertRow(-1);
      var cell = row.insertCell(0);
      cell.textContent = items[j].name;
      cell.className = 'pagespeed-stats-name';
      cell = row.insertCell(1);
      cell.textContent = items[j].value;
      cell.className = 'pagespeed-stats-value';
      cell = row.insertCell(2);
      var groupId = pagespeed.Statistics.groupForName_(items[j].name);
      var groupTitle = pagespeed.Statistics.groupTitleForId_(groupId);
      var badge = document.createElement('span');
      badge.className = 'pagespeed-stat-subsystem pagespeed-stat-subsystem-' +
          groupId;
      badge.textContent = groupTitle;
      cell.appendChild(badge);
      cell = row.insertCell(3);
      cell.textContent = pagespeed.Statistics.descriptionForName_(items[j].name);
      cell.className = 'pagespeed-stats-description';
      cell = row.insertCell(4);
      cell.textContent = this.timesOfChange_[items[j].name].toString();
      cell.className = 'pagespeed-stats-number-of-changes';
    }
    var tableWrap = document.createElement('div');
    tableWrap.className = 'pagespeed-admin-table-wrap';
    tableWrap.appendChild(table);
    section.appendChild(tableWrap);
    statElement.appendChild(section);
  }
  if (!messages.length) {
    var empty = document.createElement('p');
    empty.className = 'pagespeed-stat-empty';
    empty.textContent = this.filter_ ? 'No metrics match this filter.' :
                                      'No statistics are available.';
    statElement.appendChild(empty);
  }
};


/** Update the small live summary cards and cache activity chart. */
pagespeed.Statistics.prototype.renderSummary_ = function() {
  var values = {};
  for (var i = 0; i < this.psolMessages_.length; i++) {
    values[this.psolMessages_[i].name] = Number(this.psolMessages_[i].value) || 0;
  }
  var metricSpecs = [
    ['Cache hits', 'cache_hits', 'Cache results served from memory', 'green'],
    ['Cache misses', 'cache_misses', 'Requests requiring a cache lookup', 'red'],
    ['Backend hits', 'cache_backend_hits', 'Successful backend cache reads', 'blue'],
    ['Backend misses', 'cache_backend_misses', 'Backend cache lookups without an entry', 'gold'],
    ['Cache time', 'cache_time_us', 'Cumulative cache time', 'amber']
  ];
  var summary = document.getElementById('pagespeed-stat-summary');
  while (summary.firstChild) summary.removeChild(summary.firstChild);
  for (var i = 0; i < metricSpecs.length; i++) {
    var card = document.createElement('article');
    card.className = 'pagespeed-stat-metric pagespeed-stat-metric-' +
        metricSpecs[i][3];
    var label = document.createElement('span');
    label.className = 'pagespeed-stat-metric-label';
    label.textContent = metricSpecs[i][0];
    var value = document.createElement('strong');
    value.className = 'pagespeed-stat-metric-value';
    value.textContent = metricSpecs[i][1] == 'cache_time_us' ?
        ((values[metricSpecs[i][1]] || 0) / 1000).toFixed(1) + ' ms' :
        (values[metricSpecs[i][1]] || 0).toLocaleString();
    var note = document.createElement('span');
    note.className = 'pagespeed-stat-metric-note';
    note.textContent = metricSpecs[i][2];
    card.appendChild(label); card.appendChild(value); card.appendChild(note);
    summary.appendChild(card);
  }
  this.renderCacheActivity_(values);
  this.renderOptimizationActivity_(values);
};


/** Draw a cache hits/misses ring and its legend. */
pagespeed.Statistics.prototype.renderCacheActivity_ = function(values) {
  var hits = values['cache_hits'] || 0;
  var misses = values['cache_misses'] || 0;
  var total = hits + misses;
  var share = total ? 100 * hits / total : 0;
  var donut = document.getElementById('pagespeed-stat-donut');
  donut.style.background = total ?
      'conic-gradient(#4e876d 0 ' + share + '%, #d77665 ' + share + '% 100%)' :
      '#e8eee9';
  document.getElementById('pagespeed-stat-donut-center').innerHTML =
      '<strong>' + total.toLocaleString() + '</strong><span>Total requests</span>';
  var legend = document.getElementById('pagespeed-stat-cache-legend');
  while (legend.firstChild) legend.removeChild(legend.firstChild);
  var entries = [['Hits', hits, '#4e876d'], ['Misses', misses, '#d77665']];
  for (var i = 0; i < entries.length; i++) {
    var item = document.createElement('div');
    item.className = 'pagespeed-stat-legend-row';
    var swatch = document.createElement('i');
    swatch.style.background = entries[i][2];
    var label = document.createElement('span');
    label.textContent = entries[i][0] + ' (' +
        (total ? (100 * entries[i][1] / total).toFixed(1) : '0.0') + '%)';
    var number = document.createElement('strong');
    number.textContent = entries[i][1].toLocaleString();
    item.appendChild(swatch); item.appendChild(label); item.appendChild(number);
    legend.appendChild(item);
  }
};


/** Draw compact horizontal bars for representative optimization counters. */
pagespeed.Statistics.prototype.renderOptimizationActivity_ = function(values) {
  var specs = [
    ['CSS filter uses', 'css_filter_uses'],
    ['Image rewrites', 'image_rewrites'],
    ['Analytics rewrites', 'google_analytics_rewritten_count'],
    ['Critical-image beacons', 'critical_images_beacon_filter_script_added_count']
  ];
  var max = 0;
  for (var i = 0; i < specs.length; i++) {
    max = Math.max(max, values[specs[i][1]] || 0);
  }
  var container = document.getElementById('pagespeed-stat-optimization');
  while (container.firstChild) container.removeChild(container.firstChild);
  for (var i = 0; i < specs.length; i++) {
    var value = values[specs[i][1]] || 0;
    var row = document.createElement('div');
    row.className = 'pagespeed-stat-activity-row';
    var label = document.createElement('span');
    label.textContent = specs[i][0];
    var track = document.createElement('span');
    track.className = 'pagespeed-stat-activity-track';
    var bar = document.createElement('i');
    bar.style.width = (max ? 100 * value / max : 0) + '%';
    track.appendChild(bar);
    var number = document.createElement('strong');
    number.textContent = value.toLocaleString();
    row.appendChild(label); row.appendChild(track); row.appendChild(number);
    container.appendChild(row);
  }
};


/** Refresh category counts and selected state. */
pagespeed.Statistics.prototype.updateCategoryTabs_ = function() {
  var counts = {all: this.psolMessages_.length, cache: 0, css: 0,
                images: 0, fonts: 0, other: 0};
  for (var i = 0; i < this.psolMessages_.length; i++) {
    var category = pagespeed.Statistics.categoryForName_(
        this.psolMessages_[i].name);
    counts[category]++;
  }
  var buttons = document.querySelectorAll(
      '#pagespeed-stat-category-tabs [data-category]');
  for (var i = 0; i < buttons.length; i++) {
    var category = buttons[i].getAttribute('data-category');
    var label = category == 'all' ? 'All counters' :
        category.charAt(0).toUpperCase() + category.substring(1);
    buttons[i].textContent = label + ' (' + counts[category] + ')';
    buttons[i].className = category == this.activeCategory_ ? 'is-active' : '';
    buttons[i].setAttribute('aria-selected',
                            category == this.activeCategory_ ? 'true' : 'false');
  }
};


pagespeed.Statistics.prototype.setCategory_ = function(category) {
  this.activeCategory_ = category;
  this.update();
};


/**
 * @param {string} name Statistic name.
 * @return {string} The functional group for a statistic name.
 * @private
 */
pagespeed.Statistics.groupForName_ = function(name) {
  var normalized = name.toLowerCase();
  if (/^(image|webp|avif)_/.test(normalized)) return 'images';
  if (/font/.test(normalized)) return 'fonts';
  if (/^(css|style|stylesheet)_/.test(normalized)) return 'css';
  if (/^(javascript|js)_/.test(normalized)) return 'javascript';
  if (/(cache|purge|metadata|property_cache|ipro)/.test(normalized)) {
    return 'cache';
  }
  if (/^(fetch|http|https|url|dns|ssl|proxy|serf|upstream)_/.test(normalized)) {
    return 'network';
  }
  if (/^(html|rewrite|beacon|critical|filter|resource)_/.test(normalized)) {
    return 'rewriting';
  }
  return 'runtime';
};


/** @param {string} name Statistic name. @return {string} Filter category. */
pagespeed.Statistics.categoryForName_ = function(name) {
  var normalized = name.toLowerCase();
  if (/font/.test(normalized)) return 'fonts';
  var group = pagespeed.Statistics.groupForName_(name);
  if (group == 'cache') return 'cache';
  if (group == 'css') return 'css';
  if (group == 'images') return 'images';
  return 'other';
};


/** @param {string} name Statistic name. @param {string} category Category. */
pagespeed.Statistics.matchesCategory_ = function(name, category) {
  return category == 'all' ||
      pagespeed.Statistics.categoryForName_(name) == category;
};


/** @param {string} id Group id. @return {string} Display label. */
pagespeed.Statistics.groupTitleForId_ = function(id) {
  for (var i = 0; i < pagespeed.Statistics.GROUPS_.length; i++) {
    if (pagespeed.Statistics.GROUPS_[i].id == id) {
      return pagespeed.Statistics.GROUPS_[i].title;
    }
  }
  return 'Other';
};


/** @param {string} name Statistic name. @return {string} Description. */
pagespeed.Statistics.descriptionForName_ = function(name) {
  var descriptions = {
    cache_time_us: 'Cumulative time spent in cache operations (microseconds).',
    cache_hits: 'Number of requests served from the PageSpeed cache.',
    cache_misses: 'Number of requests that missed the PageSpeed cache.',
    cache_backend_hits: 'Number of successful backend cache reads.',
    cache_backend_misses: 'Number of backend cache lookups without an entry.',
    cache_fallbacks: 'Number of times processing fell back to the origin.',
    cache_expirations: 'Number of expired entries encountered in the cache.',
    cache_inserts: 'Number of items inserted into the cache.',
    cache_deletes: 'Number of items deleted from the cache.',
    css_filter_uses: 'Number of requests that used a CSS rewrite filter.',
    css_filter_blocks_rewritten: 'Number of CSS blocks successfully rewritten.',
    image_rewrites: 'Number of image resources successfully rewritten.',
    image_rewrite_total_bytes_saved: 'Cumulative bytes saved by image rewrites.',
    javascript_blocks_minified: 'Number of JavaScript blocks minified.',
    javascript_total_bytes_saved: 'Cumulative bytes saved by JavaScript rewrites.',
    critical_images_beacon_filter_script_added_count:
        'Critical-image beacon scripts added to pages.'
  };
  if (descriptions[name]) return descriptions[name];
  return 'PageSpeed counter tracking ' + name.replace(/_/g, ' ') + '.';
};


/** @private {!Array<!{id: string, title: string, description: string}>} */
pagespeed.Statistics.GROUPS_ = [
  {id: 'images', title: 'Image Optimization',
    description: 'Image rewrites, format conversions, and byte savings.'},
  {id: 'fonts', title: 'Font Optimization',
    description: 'Font resource detection, rewriting, and delivery.'},
  {id: 'css', title: 'CSS Optimization',
    description: 'Stylesheet combination, rewriting, and minification.'},
  {id: 'javascript', title: 'JavaScript Optimization',
    description: 'Script rewriting, minification, and combination.'},
  {id: 'cache', title: 'Caching & Resource Delivery',
    description: 'Cache hits, invalidations, and in-place resource delivery.'},
  {id: 'network', title: 'Fetching & Network',
    description: 'Origin fetches, network activity, and response handling.'},
  {id: 'rewriting', title: 'HTML & Rewrite Pipeline',
    description: 'HTML processing, rewrite decisions, and beacon activity.'},
  {id: 'runtime', title: 'Runtime & Other',
    description: 'Process-level counters and metrics outside the groups above.'}
];


/**
 * The comparison function to be passed into JS Array sort() method to
 * sort the statistics in alphabetic and ascending order.
 * @param {pagespeed.StatsNode} a The node to compare.
 * @param {pagespeed.StatsNode} b The node to compare.
 * @return {number} The return value of the comparison.
 */
pagespeed.Statistics.alphabetCompareFunc = function(a, b) {
  if (a.name > b.name) {
    return 1;
  } else if (a.name < b.name) {
    return -1;
  } else {
    return 0;
  }
};


/**
 * Sort the statistics by the number of changes.
 */
pagespeed.Statistics.prototype.sortByNumberOfChanges = function() {
  var compareFunc = function(a, b) {
    var first = this.timesOfChange_[a.name];
    var second = this.timesOfChange_[b.name];
    // Sort in the descending order of number of changes.
    if (second != first) {
      return second - first;
    } else {
      // Otherwise, sort in alphabetical order.
      return pagespeed.Statistics.alphabetCompareFunc(a, b);
    }
  };
  this.psolMessages_.sort(goog.bind(compareFunc, this));
};


/**
 * Sort the statistics in alphabetical order.
 */
pagespeed.Statistics.prototype.sortByAlphabet = function() {
  this.psolMessages_.sort(pagespeed.Statistics.alphabetCompareFunc);
};


/**
 * Parses new statistics from the server response.
 * @param {!Object} jsonData The data dumped in JSON format.
 */
pagespeed.Statistics.prototype.parseMessagesFromResponse = function(jsonData) {
  var variables = jsonData['variables'];
  var maxLength = jsonData['maxlength'];

  // Check if the response is valid JSON.
  if (goog.typeOf(variables) != 'object' ||
      goog.typeOf(maxLength) != 'number') {
    this.error();
    return;
  }

  var messages = [];
  for (var name in variables) {
    // Remove elements with trailing underscores.
    if (name[name.length - 1] == '_') continue;
    // For visual consistency, use underscores instead of a mix of dashes and
    // underscores.
    var trimmedName = name.replace(/-/g, '_');
    // Ignore the leading underscore.
    if (trimmedName[0] == '_') {
      trimmedName = trimmedName.substring(1);
    }
    var node = {
      name: trimmedName,
      value: variables[name].toString()
    };
    messages.push(node);

    // If the timesOfChange vector is uninitialized, we'll start
    // it at 1 if the statistics value is non-zero.  This is not always
    // correct because some stats might shrink back to zero, but this
    // number-of-changes-count is an estimation anyway, and I believe this
    // produces a more useful sort-by-number-of-changes function.
    var newValue = variables[name];
    var prevChangeCount = this.timesOfChange_[trimmedName];
    if (prevChangeCount == undefined) {
      if (newValue) {           // Not undefined, and not 0.
        this.timesOfChange_[trimmedName] = 1;
      } else {
        this.timesOfChange_[trimmedName] = 0;
      }
    } else if (newValue != this.lastValue_[trimmedName]) {
      this.timesOfChange_[trimmedName] = prevChangeCount + 1;
    }
    this.lastValue_[trimmedName] = newValue;
  }
  this.psolMessages_ = messages;
  this.numUnfilteredMessages_ = messages.length;
  this.update();
};


/**
  * The error message of auto-refresh.
  * @private {string}
  * @const
  */
pagespeed.Statistics.REFRESH_ERROR_ =
    'Sorry, failed to update the statistics. ' +
    'Please wait and try again later.';


/**
 * Capture the pathname and return the URL for request.
 * @return {string} The URL to request the updated data in JSON format.
 */
pagespeed.Statistics.prototype.requestUrl = function() {
  var pathName = location.pathname;
  // Ignore the trailing '/'.
  var n = pathName.lastIndexOf('/', pathName.length - 2);
  // e.g. /pagespeed_admin/foo or pagespeed_admin/foo
  if (n > 0) {
    return pathName.substring(0, n) + '/stats_json';
  } else {
    // e.g. /pagespeed_admin or pagespeed_admin
    return pathName + '/stats_json';
  }
};


/**
 * Refreshes the page by making requsts to server.
 */
pagespeed.Statistics.prototype.performRefresh = function() {
  if (!this.xhr_.isActive()) {
    if (!this.firstRefreshStarted_ || this.autoRefresh_) {
      this.firstRefreshStarted_ = true;
      this.xhr_.send(this.requestUrl());
    }
  }
};


/**
 * Parses the response sent by server and updates the page.
 */
pagespeed.Statistics.prototype.parseAjaxResponse = function() {
  if (this.xhr_.isSuccess()) {
    var jsonData = this.xhr_.getResponseJson();
    this.parseMessagesFromResponse(jsonData);
  } else {
    console.log(this.xhr_.getLastError());
    this.error();
  }
};


/**
 * The frequency of auto-refresh. Default is once per 5 seconds.
 * @private {number}
 * @const
 */
pagespeed.Statistics.FREQUENCY_ = 5 * 1000;


/**
 * The Main entry to start processing.
 * @export
 */
pagespeed.Statistics.Start = function() {
  var statisticsOnload = function() {
    var statisticsObj = new pagespeed.Statistics();
    var filterElement = document.getElementById('text-filter');
    goog.events.listen(
        filterElement, 'input', goog.bind(statisticsObj.setFilter,
                                          statisticsObj, filterElement));
    goog.events.listen(document.getElementById('auto-refresh'), 'change',
                       goog.bind(statisticsObj.toggleAutorefresh,
                                 statisticsObj));
    setInterval(goog.bind(statisticsObj.performRefresh, statisticsObj),
                pagespeed.Statistics.FREQUENCY_);
    goog.events.listen(
        statisticsObj.xhr_, goog.net.EventType.COMPLETE,
        goog.bind(statisticsObj.parseAjaxResponse, statisticsObj));
    statisticsObj.performRefresh();
  };
  goog.events.listen(window, 'load', statisticsOnload);
};
