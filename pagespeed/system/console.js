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

/**
 * @fileoverview Code for running PSOL Console.
 *
 * Fetches JSON statistics data from server to draw graphs over time of
 * various "notable issues".
 *
 * Note that for unit testing purposes, the initialization code here is not
 * actually run. admin_site.cc injects the call to
 * pagespeed.startConsole() to run the code here.
 *
 * PRECONDITION: pagespeedStatisticsUrl must be set in JavaScript.
 *
 * @author sligocki@google.com (Shawn Ligocki)
 * @author sarahdw@google.com (Sarah Dapul-Weberman)
 * @author bvb@google.com (Ben VanBerkum)
 */

'use strict';

goog.provide('pagespeed');
goog.provide('pagespeed.Console');
goog.provide('pagespeed.statistics');

goog.require('goog.structs.Set');


/**
 * Report error.
 * @param {string} error_message
 */
pagespeed.error = function(error_message) {
  if (window.console) {
    console.error(error_message);
  }
  var status = document.getElementById('pagespeed-console-status');
  if (status) {
    status.textContent = error_message;
    status.setAttribute('data-state', 'error');
  }
};



/**
 * @constructor
 */
pagespeed.Console = function() {
  /**
   * Specifications for graphs. Add a new graph with addGraph().
   * @type {Array.<Object>}
   * @private
   */
  this.graphs_ = [];

  /**
   * Names of variables needed for loading all graphs. Set by addGraph(),
   * used in request to pagespeed server.
   * @type {goog.structs.Set.<string>}
   * @private
   */
  this.varsNeeded_ = new goog.structs.Set();

  /**
   * Mapping of variable names -> array of values over time.
   * Each value is a snapshot of that variable at the timestamp in the
   * parallel data structure this.timestamps_.
   * @type {Object.<string, Array.<number> >}
   * @private
   */
  this.variables_ = null;

  /**
   * Array of timestamps, one for every set of variables.
   * @type {Array.<number>}
   * @private
   */
  this.timestamps_ = null;


};


/**
 * Namespace for statistics calculators.
 */
pagespeed.statistics = {};


/**
 * Trivial statistic that just has the value of one variable.
 * For example: Cache hits.
 *
 * TODO(sligocki): Type check the return value against a predefined type,
 * pagespeed.Statistic, so that we can guarantee that the returned object
 * has the appropriate members and type (s/Object/pagespeed.Statistic/).
 *
 * @param {string} name  Variable name to attach to.
 * @return {Object}  Just this variable.
 */
pagespeed.statistics.variable = function(name) {
  var stat = {};
  stat.varsNeeded = new goog.structs.Set([name]);
  /**
   * @param {function(string): number} variableGetter
   * @return {number}
   */
  stat.evaluate = function(variableGetter) {
    return variableGetter(name);
  };
  return stat;
};


/**
 * Statistic which has the value of a sum of sub-statistics.
 * For example: Total cache requests = cache hits + cache misses.
 *
 * @param {Array.<Object>} statArray  Statistics to add up.
 * @return {Object}  Statistic representing summed result.
 */
pagespeed.statistics.sum = function(statArray) {
  var stat = {};
  stat.varsNeeded = new goog.structs.Set();
  for (var i = 0; i < statArray.length; i++) {
    stat.varsNeeded.addAll(statArray[i].varsNeeded);
  }
  /**
   * @param {function(string): number} variableGetter
   * @return {number}
   */
  stat.evaluate = function(variableGetter) {
    var value = 0;
    for (var i = 0; i < statArray.length; i++) {
      value += statArray[i].evaluate(variableGetter);
    }
    return value;
  };
  return stat;
};


/**
 * Statistic which has the value of a percent of sub-statistics.
 * For example: Cache hit percent = (cache hits) / (total cache requests).
 *
 * @param {Object} numStat    Numerator statistic.
 * @param {Object} denomStat  Denominator statistic.
 * @return {Object}  Statistic representing percent result.
 */
pagespeed.statistics.percent = function(numStat, denomStat) {
  var stat = {};
  stat.varsNeeded = new goog.structs.Set();
  stat.varsNeeded.addAll(numStat.varsNeeded);
  stat.varsNeeded.addAll(denomStat.varsNeeded);
  /**
   * @param {function(string): number} variableGetter
   * @return {number}
   */
  stat.evaluate = function(variableGetter) {
    var denom = denomStat.evaluate(variableGetter);
    if (denom == 0) {
      return 0.0;
    } else {
      return numStat.evaluate(variableGetter) / denom;
    }
  };
  return stat;
};


/**
 * Statistic for common pattern: bad / (bad + good)
 * For example: Cache miss % = cache misses / (cache misses + cache hits)
 *
 * @param {Object} badStat   Numerator statistic.
 * @param {Object} goodStat  Added to badStat to get denominator.
 * @return {Object}  Statistic representing percent result.
 */
pagespeed.statistics.percent_total = function(badStat, goodStat) {
  return pagespeed.statistics.percent(
      badStat,
      pagespeed.statistics.sum([badStat, goodStat]));
};


/**
 * Initialize and start the console for "notable issues" version, which
 * displays a fixed set of graphs ordered by level of importance.
 *
 * @return {pagespeed.Console}  The initialized console object.
 * @export
 */
pagespeed.startConsole = function() {
  var mpsConsole = new pagespeed.Console();
  mpsConsole.initGraphs();
  mpsConsole.startConsole();
  return mpsConsole;
};


/**
 * Initialize pre-determined set of "notable issues" graphs.
 */
pagespeed.Console.prototype.initGraphs = function() {
  var v = pagespeed.statistics.variable;
  var sum = pagespeed.statistics.sum;
  var percent = pagespeed.statistics.percent;
  var percent_total = pagespeed.statistics.percent_total;

  this.addGraph('Resources not loaded because of fetch failures',
                'fetch-failure',
                percent(v('serf_fetch_failure_count'),
                        v('serf_fetch_request_count')));
  this.addGraph("Resources not rewritten because domain wasn't authorized",
                'not-authorized',
                percent_total(v('resource_url_domain_rejections'),
                              v('resource_url_domain_acceptances')));
  this.addGraph('Resources not rewritten because of restrictive ' +
                    'Cache-Control headers',
                'cache-control',
                percent_total(v('num_cache_control_not_rewritable_resources'),
                              v('num_cache_control_rewritable_resources')));
  var totalCacheCalls = sum([v('cache_backend_misses'),
                             v('cache_backend_hits')]);
  this.addGraph('Cache misses',
                'cache-miss',
                percent(v('cache_backend_misses'), totalCacheCalls));
  this.addGraph('Cache lookups that were expired',
                'cache-expired',
                percent(v('cache_expirations'), totalCacheCalls));
  this.addGraph('CSS files not rewritten because of parse errors',
                'css-error',
                percent_total(v('css_filter_parse_failures'),
                              v('css_filter_blocks_rewritten')));
  this.addGraph('JavaScript minification failures',
                'js-error',
                percent_total(v('javascript_minification_failures'),
                              v('javascript_blocks_minified')));
  var goodImageResults =
      sum([v('image_rewrites'),
           v('image_rewrites_dropped_nosaving_resize'),
           v('image_rewrites_dropped_nosaving_noresize')]);
  var badImageResults =
      sum([v('image_norewrites_high_resolution'),
           v('image_rewrites_dropped_decode_failure'),
           v('image_rewrites_dropped_due_to_load'),
           v('image_rewrites_dropped_mime_type_unknown'),
           v('image_rewrites_dropped_server_write_fail')]);
  this.addGraph('Image rewrite failures',
                'image-error',
                percent_total(badImageResults, goodImageResults));
  /* TODO(sligocki): Get CSS combine stat working.
     Note: This stat is also generally much higher than the rest and also
     less important, we should de-prioritize it as well.
  this.addGraph('CSS combine opportunities missed',
                'css-combine-error',
                percent(
      v('css_combine_opportunities') - v('css_file_count_reduction'),
      v('css_combine_opportunities')));
  */
};


/**
 * Add a graph specification to our list. Also note which variables we will
 * need to fetch.
 *
 * @param {string} title  Name of the graph.
 * @param {string} urlFragment  Id on documentation page to link to.
 * @param {Object} stat   Statistic to graph.
 * @return {Object}  The graph spec (used in tests).
 */
pagespeed.Console.prototype.addGraph = function(title, urlFragment, stat) {
  var graph = {};
  graph.title = title;
  graph.docUrl =
      'https://modpagespeed.com/doc/console#' +
          urlFragment;
  graph.value = stat;
  // Unique identifying number.
  graph.num = this.graphs_.length;
  // Added to title, only set once data is loaded.
  graph.overallPercent = null;
  graph.priority = null;
  // Created once data is loaded.
  graph.values = null;

  this.graphs_.push(graph);
  this.varsNeeded_.addAll(stat.varsNeeded);

  return graph;
};


/**
 * Load all graphs over default time period (last day).
 */
pagespeed.Console.prototype.startConsole = function() {
  var endTime = new Date();
  var durationMs = 24 * 60 * 60 * 1000;  // 1 Day
  var startTime = new Date(endTime - durationMs);
  var granularityMs = 60 * 1000;  // 1 Minute

  this.loadJsonData(startTime, endTime, granularityMs);
};


/**
 * Generate a URL which will request specific variables values snapshotted
 * over a given timeframe with a specified granularity.
 *
 * @param {Array.<string>} varNames  Array of variable names to request.
 * @param {Date} startTime  Begining of timeframe.
 * @param {Date} endTime    End of timeframe.
 * @param {number} granularityMs  Time between data points requested.
 * @return {string}  URL incorporating all these values.
 */
pagespeed.Console.prototype.createQueryUrl = function(
    varNames, startTime, endTime, granularityMs) {
  var queryString = pagespeedStatisticsUrl + '?json';
  queryString += '&start_time=' + startTime.getTime();
  queryString += '&end_time=' + endTime.getTime();
  queryString += '&granularity=' + granularityMs;

  queryString += '&var_titles=';
  for (var i = 0; i < varNames.length; i++) {
    queryString += varNames[i] + ',';
  }
  return queryString;
};


/**
 * Request variable data from server. Parse the returned result and call
 * updateGraphsFromJsonData() with resulting data.
 *
 * @param {Date} startTime  Begining of timeframe.
 * @param {Date} endTime    End of timeframe.
 * @param {number} granularityMs  Time between data points requested.
 */
pagespeed.Console.prototype.loadJsonData = function(
    startTime, endTime, granularityMs) {
  var xhr = new XMLHttpRequest();
  var mpsConsole = this;
  // TODO(sligocki): varsNeeded list is getting long, change protocol so that
  // JSON requests just return all vars tracked. That would (a) keep the URLs
  // shorter and (b) remove our need to keep track of varsNeeded.
  var queryString = this.createQueryUrl(
      this.varsNeeded_.getValues(), startTime, endTime, granularityMs);

  xhr.onreadystatechange = function() {
    if (this.readyState != 4) {
      return;
    }
    if (this.status != 200 || this.responseText.length < 1 ||
        this.responseText[0] != '{') {
      pagespeed.error('XHR request failed.');
      return;
    }
    var json_data = JSON.parse(this.responseText);
    mpsConsole.drawGraphsFromJsonData(json_data);
  };

  xhr.open('GET', queryString);
  xhr.send();
};


/**
 * Use JSON data to create and draw all graphs.
 * TODO(sligocki): Allow updating graphs, not just adding new ones.
 *
 * @param {*} data  Parsed JSON data from backend.
 */
pagespeed.Console.prototype.drawGraphsFromJsonData = function(data) {
  this.variables_ = data['variables'];
  // TODO(sligocki): Convert to {Array.<Date>}.
  this.timestamps_ = data['timestamps'];

  this.checkDataValidity(this.timestamps_, this.variables_);

  for (var i = 0; i < this.graphs_.length; i++) {
    // Each graph is a collection of (x, y) points where x is a timestamp
    // and y is the stat at that time. statTimeSeries stores those y values.
    // TODO(sligocki): Perhaps we should instead show the stat computed from
    // diffs from the last timestamp. That way changes should be notable.
    // Or maybe both.
    var statTimeSeries = [];
    for (var j = 0; j < this.timestamps_.length; j++) {
      statTimeSeries.push(this.graphs_[i].value.evaluate(
          function(variables) {
            /**
             * @param {string} name  variable name.
             * @return {number}  Variable value at timestamp j.
             */
            var fn = function(name) {
              if (name in variables) {
                return variables[name][j];
              } else {
                pagespeed.error('JSON data missing required variable.');
                return 0;
              }
            };
            return fn;
          }(this.variables_)));
    }
    this.graphs_[i].values = statTimeSeries;
    this.graphs_[i].timestamps = this.timestamps_;
    this.graphs_[i].overallPercent = statTimeSeries.length ?
        statTimeSeries[statTimeSeries.length - 1] : 0;
    // TODO(sligocki): This just sets the priority equal to the overall
    // long-run stat average. But we may want to prioritize different
    // issues different amounts.
    this.graphs_[i].priority = this.graphs_[i].overallPercent;

  }

  // Sort by priority (highest priority first).
  this.graphs_.sort(function(a, b) { return b.priority - a.priority; });

  for (var i = 0; i < this.graphs_.length; i++) {
    this.drawGraph(this.graphs_[i]);
  }
  this.renderSummary_();
  this.installSearch_();
  var status = document.getElementById('pagespeed-console-status');
  if (status) {
    status.textContent = this.timestamps_.length ?
        'Updated just now - ' + this.timestamps_.length + ' samples across 24 hours' :
        'No samples are available for the selected period.';
    status.removeAttribute('data-state');
  }
};


/**
 * Error if any variables has an inconsistent number of data points.
 * All variables should have the same number of values as there are timestamps.
 *
 * @param {Array} timestamps
 * @param {Object.<Array>} variables
 */
pagespeed.Console.prototype.checkDataValidity = function(
    timestamps, variables) {
  for (var name in variables) {
    if (timestamps.length != variables[name].length) {
      pagespeed.error('JSON response is malformed. (' + timestamps.length +
                      ' != ' + variables[name].length + ')');
    }
  }
};


/**
 * Draw an accessible responsive SVG chart for a graph.
 * @param {Object} graph Graph specification and its sampled values.
 */
pagespeed.Console.prototype.drawGraph = function(graph) {
  var chart = pagespeed.createGraphDiv(graph);
  var values = graph.values || [];
  var timestamps = graph.timestamps || [];
  var ns = 'http://www.w3.org/2000/svg';
  var width = 760, height = 220, left = 48, right = 14, top = 12, bottom = 28;
  var plotWidth = width - left - right, plotHeight = height - top - bottom;
  var max = 0;
  for (var i = 0; i < values.length; i++) max = Math.max(max, values[i]);
  max = Math.max(max * 1.15, 0.01);
  var svg = document.createElementNS(ns, 'svg');
  svg.setAttribute('class', 'pagespeed-console-chart');
  svg.setAttribute('viewBox', '0 0 ' + width + ' ' + height);
  // No role="img": axis and time labels below stay navigable; the
  // visually-hidden table after the chart carries sampled values.
  svg.setAttribute('aria-label', graph.title + ' over the last 24 hours');
  var defs = document.createElementNS(ns, 'defs');
  var gradient = document.createElementNS(ns, 'linearGradient');
  gradient.setAttribute('id', 'pagespeed-console-gradient-' + graph.num);
  gradient.setAttribute('x1', '0'); gradient.setAttribute('x2', '0');
  gradient.setAttribute('y1', '0'); gradient.setAttribute('y2', '1');
  var stop1 = document.createElementNS(ns, 'stop');
  stop1.setAttribute('offset', '0%'); stop1.setAttribute('stop-color', '#39765e');
  stop1.setAttribute('stop-opacity', '.22');
  var stop2 = document.createElementNS(ns, 'stop');
  stop2.setAttribute('offset', '100%'); stop2.setAttribute('stop-color', '#39765e');
  stop2.setAttribute('stop-opacity', '0');
  gradient.appendChild(stop1); gradient.appendChild(stop2); defs.appendChild(gradient);
  svg.appendChild(defs);
  for (var tick = 0; tick <= 4; tick++) {
    var y = top + plotHeight * tick / 4;
    var grid = document.createElementNS(ns, 'line');
    grid.setAttribute('x1', left); grid.setAttribute('x2', width - right);
    grid.setAttribute('y1', y); grid.setAttribute('y2', y);
    grid.setAttribute('class', 'pagespeed-console-gridline'); svg.appendChild(grid);
    var label = document.createElementNS(ns, 'text');
    label.setAttribute('x', left - 8); label.setAttribute('y', y + 4);
    label.setAttribute('text-anchor', 'end');
    label.setAttribute('class', 'pagespeed-console-axis-label');
    label.textContent = (100 * max * (1 - tick / 4)).toFixed(1) + '%';
    svg.appendChild(label);
  }
  var linePath = '';
  for (var i = 0; i < values.length; i++) {
    var x = left + (values.length < 2 ? plotWidth / 2 :
        plotWidth * i / (values.length - 1));
    var pointY = top + plotHeight * (1 - values[i] / max);
    linePath += (i ? ' L ' : 'M ') + x.toFixed(2) + ' ' + pointY.toFixed(2);
  }
  if (values.length) {
    var area = document.createElementNS(ns, 'path');
    area.setAttribute('d', linePath + ' L ' + (left + plotWidth) + ' ' +
        (top + plotHeight) + ' L ' + left + ' ' + (top + plotHeight) + ' Z');
    area.setAttribute('class', 'pagespeed-console-area');
    area.setAttribute('fill', 'url(#pagespeed-console-gradient-' + graph.num + ')');
    svg.appendChild(area);
    var line = document.createElementNS(ns, 'path');
    line.setAttribute('d', linePath); line.setAttribute('class', 'pagespeed-console-line');
    svg.appendChild(line);
    var stride = Math.max(1, Math.floor(values.length / 12));
    for (var i = 0; i < values.length; i += stride) {
      var cx = left + (values.length < 2 ? plotWidth / 2 :
          plotWidth * i / (values.length - 1));
      var cy = top + plotHeight * (1 - values[i] / max);
      var circle = document.createElementNS(ns, 'circle');
      circle.setAttribute('cx', cx); circle.setAttribute('cy', cy);
      circle.setAttribute('r', '3'); circle.setAttribute('class', 'pagespeed-console-point');
      var title = document.createElementNS(ns, 'title');
      title.textContent = new Date(timestamps[i]).toLocaleString() + ': ' +
          (100 * values[i]).toFixed(2) + '%';
      circle.appendChild(title); svg.appendChild(circle);
    }
  }
  var start = document.createElementNS(ns, 'text');
  start.setAttribute('x', left); start.setAttribute('y', height - 5);
  start.setAttribute('class', 'pagespeed-console-axis-label');
  start.textContent = timestamps.length ? new Date(timestamps[0]).toLocaleTimeString([],
      {hour: 'numeric', minute: '2-digit'}) : '';
  svg.appendChild(start);
  var end = document.createElementNS(ns, 'text');
  end.setAttribute('x', width - right); end.setAttribute('y', height - 5);
  end.setAttribute('text-anchor', 'end'); end.setAttribute('class', 'pagespeed-console-axis-label');
  end.textContent = timestamps.length ? new Date(timestamps[timestamps.length - 1]).toLocaleTimeString([],
      {hour: 'numeric', minute: '2-digit'}) : '';
  svg.appendChild(end);
  chart.appendChild(svg);
  var dataTable = document.createElement('table');
  dataTable.className = 'pagespeed-visually-hidden';
  var caption = document.createElement('caption');
  caption.textContent = graph.title + ', sampled values';
  dataTable.appendChild(caption);
  var headRow = document.createElement('tr');
  var timeHead = document.createElement('th');
  timeHead.textContent = 'Time';
  headRow.appendChild(timeHead);
  var valueHead = document.createElement('th');
  valueHead.textContent = 'Value';
  headRow.appendChild(valueHead);
  dataTable.appendChild(headRow);
  var stride = Math.max(1, Math.floor(values.length / 12));
  for (var s = 0; s < values.length; s += stride) {
    var bodyRow = document.createElement('tr');
    var timeCell = document.createElement('td');
    timeCell.textContent = timestamps.length ?
        new Date(timestamps[s]).toLocaleString() : '';
    bodyRow.appendChild(timeCell);
    var valueCell = document.createElement('td');
    valueCell.textContent = (100 * values[s]).toFixed(2) + '%';
    bodyRow.appendChild(valueCell);
    dataTable.appendChild(bodyRow);
  }
  chart.appendChild(dataTable);
};

/** Populate the four at-a-glance console metrics. */
pagespeed.Console.prototype.renderSummary_ = function() {
  var container = document.getElementById('pagespeed-console-summary');
  if (!container) return;
  while (container.firstChild) container.removeChild(container.firstChild);
  var peak = this.graphs_.length ? this.graphs_[0] : null;
  var latest = this.timestamps_.length ?
      new Date(this.timestamps_[this.timestamps_.length - 1]).toLocaleString() : 'No samples';
  var items = [
    ['Signals tracked', String(this.graphs_.length), 'Operational health indicators'],
    ['Highest current rate', peak ? (100 * peak.overallPercent).toFixed(2) + '%' : 'n/a', peak ? peak.title : 'No data'],
    ['Time window', '24 hours', this.timestamps_.length + ' recorded samples'],
    ['Latest sample', latest, 'Most recent statistics snapshot']
  ];
  for (var i = 0; i < items.length; i++) {
    var card = document.createElement('div');
    card.className = 'pagespeed-console-summary-card';
    var label = document.createElement('span'); label.className = 'pagespeed-console-summary-label';
    label.textContent = items[i][0];
    var value = document.createElement('strong'); value.className = 'pagespeed-console-summary-value';
    value.textContent = items[i][1];
    var note = document.createElement('span'); note.className = 'pagespeed-console-summary-note';
    note.textContent = items[i][2];
    card.appendChild(label); card.appendChild(value); card.appendChild(note);
    container.appendChild(card);
  }
};

/** Add client-side filtering for the plotted indicators. */
pagespeed.Console.prototype.installSearch_ = function() {
  var input = document.getElementById('pagespeed-console-search');
  if (!input || input.getAttribute('data-ready')) return;
  input.setAttribute('data-ready', 'true');
  input.addEventListener('input', function() {
    var query = input.value.toLowerCase();
    var cards = document.querySelectorAll('#pagespeed-graphs-container .pagespeed-widgets');
    for (var i = 0; i < cards.length; i++) {
      cards[i].style.display = cards[i].textContent.toLowerCase().indexOf(query) >= 0 ? '' : 'none';
    }
  });
};

// Methods for creating HTML content


/**
 * createGraphDiv creates the necessary DOM elements for a graph, and returns
 * the div in which the graph will be drawn.
 *
 * @param {string} title     The title of the graph.
 * @param {number} percent   overall percent for summary.
 * @param {string} docUrl    Documentation URL.
 * @param {number} graphNum  Unique number for this graph.
 * @return {Element}  The div in which to draw the graph.
 */
pagespeed.createGraphDiv = function(graphSpec) {
  var wholeDiv = document.createElement('div');
  wholeDiv.setAttribute('class', 'pagespeed-widgets');
  wholeDiv.appendChild(pagespeed.createGraphTitleBar(graphSpec));
  var graph = document.createElement('div');
  graph.setAttribute('class', 'pagespeed-graph');
  wholeDiv.appendChild(graph);
  var container = document.getElementById('pagespeed-graphs-container');
  if (container) container.appendChild(wholeDiv);
  return graph;
};

/** Creates the title, documentation link, and current metric value. */
pagespeed.createGraphTitleBar = function(graph) {
  var topBar = document.createElement('div');
  topBar.setAttribute('class', 'pagespeed-widgets-topbar');
  var heading = document.createElement('div');
  heading.setAttribute('class', 'pagespeed-widget-heading');
  var title = document.createElement('span');
  title.setAttribute('class', 'pagespeed-title');
  title.textContent = graph.title;
  heading.appendChild(title);
  var link = document.createElement('a');
  link.setAttribute('class', 'pagespeed-console-doc');
  link.setAttribute('href', graph.docUrl);
  link.setAttribute('target', '_blank');
  link.setAttribute('rel', 'noopener');
  link.textContent = 'View documentation';
  heading.appendChild(link);
  var valueWrap = document.createElement('div');
  var value = document.createElement('strong');
  value.setAttribute('class', 'pagespeed-widget-current');
  value.textContent = (100 * graph.overallPercent).toFixed(2) + '%';
  valueWrap.appendChild(value);
  var trend = document.createElement('span');
  trend.setAttribute('class', 'pagespeed-widget-trend');
  var first = graph.values && graph.values.length ? graph.values[0] : graph.overallPercent;
  var delta = graph.overallPercent - first;
  trend.textContent = (delta <= 0 ? '↓ ' : '↑ ') + Math.abs(delta * 100).toFixed(2) +
      ' pp over 24h';
  trend.className += delta <= 0 ? ' is-improving' : ' is-rising';
  valueWrap.appendChild(trend);
  topBar.appendChild(heading); topBar.appendChild(valueWrap);
  return topBar;
};
