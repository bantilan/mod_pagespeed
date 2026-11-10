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
 * @fileoverview Code for adding auto-refreshing bar charts and realtime
 * annotated time line on the basis of the data in statistics page.
 *
 * TODO(xqyin): Integrate this into console page.
 *
 * @author oschaaf@we-amp.com (Otto van der Schaaf)
 * @author xqyin@google.com (XiaoQian Yin)
 */

goog.provide('pagespeed.Graphs');

goog.require('goog.array');
goog.require('goog.events');
goog.require('goog.net.EventType');
goog.require('goog.net.XhrIo');
goog.require('goog.object');


/** @typedef {{name: string, value:string}} */
pagespeed.StatsNode;


/** @typedef {{messages: Array.<pagespeed.StatsNode>, timeReceived: Date}} */
pagespeed.StatsArray;



/**
 * @constructor
 * @param {goog.testing.net.XhrIo=} opt_xhr Optional mock XmlHttpRequests
 *     handler for testing.
 */
pagespeed.Graphs = function(opt_xhr) {
  /**
   * The XmlHttpRequests handler for auto-refresh. We pass a mock XhrIo
   * when testing. Default is a normal XhrIo.
   * @private {goog.net.XhrIo|goog.testing.net.XhrIo}
   */
  this.xhr_ = opt_xhr || new goog.net.XhrIo();

  /**
   * This array of arrays collects historical data from statistics log file
   * for the first refresh and updates from back end to get snapshot of the
   * current data. The i-th entry of the array is the statistics corresponds
   * to the i-th time stamp, which is an array of special nodes. Each node
   * contains certain statistics name and the value. We use the last entry to
   * generate pie charts showing the most recent data. The whole array of
   * arrays is used to generate line charts showing statistics history. We only
   * keep one-day data to avoid infinite growth of this array.
   * @private {!Array.<pagespeed.StatsArray>}
   */
  this.psolMessages_ = [];

  /**
   * The option of auto-refresh. If true, the page will automatically refresh
   * itself every 5 seconds.
   * @private {boolean}
   */
  this.autoRefresh_ = false;
  // We set the default to false because auto-refresh would impact the user
  // experience of annotated time line.

  /**
   * The flag of whether the first refresh is started. We need to call a
   * refresh when loading the page for the initialization. Default is false,
   * which means the page hasn't started the first refresh yet.
   * @private {boolean}
   */
  this.firstRefreshStarted_ = false;

  /**
   * The flag of whether the second refresh is started. Since we fetch data
   * from statistics log file for the first refresh, the data may be stale.
   * We need to do the second refresh immediately to make sure the data we are
   * showing is up-to-date.
   * @private {boolean}
   */
  this.secondRefreshStarted_ = false;

  /**
   * Maps chart titles to the chart object.
   * @private {Object}
   */
  this.chartCache_ = {};

  // Hide all div elements.
  // Keep inactive views out of layout while preserving their chart state.
  for (var i in pagespeed.Graphs.DisplayDiv) {
    var chartDiv = document.getElementById(pagespeed.Graphs.DisplayDiv[i]);
    chartDiv.className = 'pagespeed-hidden-offscreen';
  }

  // The navigation bar to switch among different display modes.
  var navElement = document.createElement('nav');
  navElement.id = 'nav-bar';
  navElement.className = 'pagespeed-sub-tabs';
  navElement.setAttribute('aria-label', 'Graph views');
  navElement.innerHTML =
      '<a id="' + pagespeed.Graphs.DisplayMode.CACHE_APPLIED +
      '" href="javascript:void(0);">' +
      'Per application cache stats</a>' +
      '<a id="' + pagespeed.Graphs.DisplayMode.CACHE_TYPE +
      '" href="javascript:void(0);">' +
      'Per type cache stats</a>' +
      '<a id="' + pagespeed.Graphs.DisplayMode.IPRO +
      '" href="javascript:void(0);">' +
      'IPRO status</a>' +
      '<a id="' + pagespeed.Graphs.DisplayMode.REWRITE_IMAGE +
      '" href="javascript:void(0);">' +
      'Image rewriting</a>' +
      '<a id="' + pagespeed.Graphs.DisplayMode.REALTIME +
      '" href="javascript:void(0);">' +
      'Realtime</a>';
  // The UI table of auto-refresh.
  var uiTable = document.createElement('div');
  uiTable.id = 'ui-div';
  uiTable.className = 'pagespeed-stat-toolbar';
  uiTable.innerHTML =
      '<label>Auto refresh <input type="checkbox" ' +
      'id="auto-refresh" ' + (this.autoRefresh_ ? 'checked' : '') +
      '></label>';
  // Keep the graph tabs, refresh control, and selected view in one panel.
  var firstChart = document.getElementById(
      pagespeed.Graphs.DisplayDiv.CACHE_APPLIED);
  var chartContainer = firstChart.parentNode;
  var workspace = document.createElement('section');
  workspace.className = 'pagespeed-graphs-workspace';
  chartContainer.insertBefore(workspace, firstChart);
  var toolbar = document.createElement('div');
  toolbar.className = 'pagespeed-graphs-toolbar';
  toolbar.appendChild(navElement);
  toolbar.appendChild(uiTable);
  workspace.appendChild(toolbar);
  for (var i in pagespeed.Graphs.DisplayDiv) {
    workspace.appendChild(document.getElementById(
        pagespeed.Graphs.DisplayDiv[i]));
  }
};


/**
 * Show the chosen div element and hide all other elements.
 * @param {string} div The chosen div element to display.
 */
pagespeed.Graphs.prototype.show = function(div) {
  // Only shows the div chosen by users.
  var i;
  for (i in pagespeed.Graphs.DisplayDiv) {
    var chartDiv = pagespeed.Graphs.DisplayDiv[i];
    if (chartDiv == div) {
      document.getElementById(chartDiv).className = '';
    } else {
      document.getElementById(chartDiv).className =
          'pagespeed-hidden-offscreen';
    }
  }

  // Underline the current tab.
  var currentTab = document.getElementById(div + '_mode');
  for (i in pagespeed.Graphs.DisplayMode) {
    var link = document.getElementById(pagespeed.Graphs.DisplayMode[i]);
    if (link == currentTab) {
      link.className = 'pagespeed-underline-link';
    } else {
      link.className = '';
    }
  }

  location.href = location.href.split('#')[0] + '#' + div;
  // TODO(xqyin): Note that there are similar changes under caches page as
  // well. We should factor out the logic and let similar codes like this
  // being shared by different pages.
};


/**
 * Parse the location URL to get its anchor part and show the div element.
 */
pagespeed.Graphs.prototype.parseLocation = function() {
  var div = location.hash.substr(1);
  if (div == '') {
    this.show(pagespeed.Graphs.DisplayDiv.CACHE_APPLIED);
  } else if (goog.object.contains(pagespeed.Graphs.DisplayDiv, div)) {
    this.show(div);
  }
};


/**
 * The option of the display mode of the graphs page. Users can switch modes
 * by the second level navigation bar shown on the page. The page would show
 * the corresponding div elements containing charts.
 * @enum {string}
 */
pagespeed.Graphs.DisplayMode = {
  CACHE_APPLIED: 'cache_applied_mode',
  CACHE_TYPE: 'cache_type_mode',
  IPRO: 'ipro_mode',
  REWRITE_IMAGE: 'image_rewriting_mode',
  REALTIME: 'realtime_mode'
};


/**
 * The id of the div element that should be displayed in each mode. Only the
 * chosen div element would be shown. Others would be hidden.
 * @enum {string}
 */
pagespeed.Graphs.DisplayDiv = {
  CACHE_APPLIED: 'cache_applied',
  CACHE_TYPE: 'cache_type',
  IPRO: 'ipro',
  REWRITE_IMAGE: 'image_rewriting',
  REALTIME: 'realtime'
};


/**
 * Updates the option of auto-refresh.
 */
pagespeed.Graphs.prototype.toggleAutorefresh = function() {
  this.autoRefresh_ = !this.autoRefresh_;
};


/**
 * Generate a URL to request statistics data over default time period.
 * @return {string} The query URL incorporating all time parameters.
 */
pagespeed.Graphs.prototype.createQueryUrl = function() {
  var granularityMs = pagespeed.Graphs.FREQUENCY_ * 1000;
  var durationMs = pagespeed.Graphs.TIMERANGE_ * granularityMs; // 1 Day.
  var endTime = new Date();
  var startTime = new Date(endTime - durationMs);
  var queryString = '?json';
  queryString += '&start_time=' + startTime.getTime();
  queryString += '&end_time=' + endTime.getTime();
  queryString += '&granularity=' + granularityMs;
  return queryString;
};


/**
 * Parses historical data sent by sever.
 * @param {string} text The statistics dumped in JSON format.
 */
pagespeed.Graphs.prototype.parseHistoricalMessages = function(text) {
  var jsonData = JSON.parse(text);
  var timestamps = jsonData['timestamps'];
  var variables = jsonData['variables'];
  for (var i = 0; i < timestamps.length; ++i) {
    var messages = [];
    for (var name in variables) {
      var node = {
        name: name,
        value: variables[name][i]
      };
      messages.push(node);
    }
    var newArray = {
      messages: messages,
      timeReceived: new Date(timestamps[i])
    };
    this.psolMessages_.push(newArray);
  }
};


/**
 * Parses the most recent data sent by sever.
 * @param {string} text The statistics dumped in JSON format.
 */
pagespeed.Graphs.prototype.parseSnapshotMessages = function(text) {
  var jsonData = JSON.parse(text);
  var variables = jsonData['variables'];
  var messages = [];
  for (var name in variables) {
    var node = {
      name: name,
      value: variables[name]
    };
    messages.push(node);
  }
  var newArray = {
    messages: messages,
    timeReceived: new Date()
  };
  this.psolMessages_.push(newArray);
};


/**
 * Capture the pathname and return the URL for request.
 * @return {string} The URL to request the updated data in JSON format.
 */
pagespeed.Graphs.prototype.requestUrl = function() {
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
pagespeed.Graphs.prototype.performRefresh = function() {
  // TODO(xqyin): Figure out if there is a potential timing issue that the
  // xhr.isActive() would be false before the callback starts.
  if (!this.xhr_.isActive()) {
    // If the first refresh has not started yet, then do the refresh no matter
    // what the autoRefresh option is. Because the page needs to send a query
    // URL to request historical data to get initialized.
    if (!this.firstRefreshStarted_) {
      this.firstRefreshStarted_ = true;
      this.xhr_.send(this.createQueryUrl());
    // The page needs to do the second refresh to finish initialization.
    // Otherwise, only refresh when the autoRefresh option is true.
    } else if (!this.secondRefreshStarted_ || this.autoRefresh_) {
      this.secondRefreshStarted_ = true;
      this.xhr_.send(this.requestUrl());
    }
  }
};


/**
 * Parses the response sent by server and draws charts.
 */
pagespeed.Graphs.prototype.parseAjaxResponse = function() {
  if (this.xhr_.isSuccess()) {
    var text = this.xhr_.getResponseText();
    if (!this.secondRefreshStarted_) {
      // We collect historical data in the first refresh and wait for the
      // second refresh to update data. We won't call functions to draw charts
      // until we make data up-to-date.
      this.parseHistoricalMessages(text);
      // Add the second refresh to the queue with no delay time. So it would be
      // implemented immediately after the first refresh completion.
      window.setTimeout(goog.bind(this.performRefresh, this), 0);
    } else {
      this.parseSnapshotMessages(text);
      // Only keep one-day statistics.
      if (this.psolMessages_.length > pagespeed.Graphs.TIMERANGE_) {
        this.psolMessages_.shift();
      }
      this.drawVisualization();
    }
  } else {
    console.log(this.xhr_.getLastError());
  }
};


/**
 * Initialization for drawing all the charts.
 */
pagespeed.Graphs.prototype.drawVisualization = function() {
  this.drawBarChart('pcache-cohorts-dom', 'Property cache dom cohorts',
                    pagespeed.Graphs.DisplayDiv.CACHE_APPLIED);
  this.drawBarChart('pcache-cohorts-beacon', 'Property cache beacon cohorts',
                    pagespeed.Graphs.DisplayDiv.CACHE_APPLIED);
  this.drawBarChart('rewrite_cached_output', 'Rewrite cached output',
                    pagespeed.Graphs.DisplayDiv.CACHE_APPLIED);
  this.drawBarChart('url_input', 'URL Input',
                    pagespeed.Graphs.DisplayDiv.CACHE_APPLIED);

  this.drawBarChart('cache', 'Cache',
                    pagespeed.Graphs.DisplayDiv.CACHE_TYPE);
  this.drawBarChart('file_cache', 'File Cache',
                    pagespeed.Graphs.DisplayDiv.CACHE_TYPE);
  this.drawBarChart('memcached', 'Memcached',
                    pagespeed.Graphs.DisplayDiv.CACHE_TYPE);
  this.drawBarChart('redis', 'Redis',
                    pagespeed.Graphs.DisplayDiv.CACHE_TYPE);
  this.drawBarChart('lru_cache', 'LRU',
                    pagespeed.Graphs.DisplayDiv.CACHE_TYPE);
  this.drawBarChart('shm_cache', 'Shared Memory',
                    pagespeed.Graphs.DisplayDiv.CACHE_TYPE);

  this.drawBarChart('ipro', 'In place resource optimization',
                    pagespeed.Graphs.DisplayDiv.IPRO);

  this.drawBarChart('image_rewrite', 'Image rewrite',
                    pagespeed.Graphs.DisplayDiv.REWRITE_IMAGE);
  this.drawBarChart('image_rewrites_dropped', 'Image rewrites dropped',
                    pagespeed.Graphs.DisplayDiv.REWRITE_IMAGE);

  this.drawHistoryChart('http', 'Http', pagespeed.Graphs.DisplayDiv.REALTIME);
  this.drawHistoryChart('file_cache', 'File Cache RT',
                        pagespeed.Graphs.DisplayDiv.REALTIME);
  this.drawHistoryChart('lru_cache', 'LRU Cache RT',
                        pagespeed.Graphs.DisplayDiv.REALTIME);
  this.drawHistoryChart('serf_fetch', 'Serf stats RT',
                        pagespeed.Graphs.DisplayDiv.REALTIME);
  this.drawHistoryChart('rewrite', 'Rewrite stats RT',
                        pagespeed.Graphs.DisplayDiv.REALTIME);
};


/**
 * Screens data to generate charts according to the setting prefix.
 * @param {string} prefix The setting prefix to match.
 * @param {string} name The name of the statistics.
 * @return {boolean} Return true if the data should be used in the chart.
 */
pagespeed.Graphs.screenData = function(prefix, name) {
  var use = true;
  if (name.indexOf(prefix) != 0) {
    use = false;
  // We skip here because the statistics below won't be used in any charts.
  } else if (name.indexOf('cache_flush_timestamp_ms') >= 0) {
    use = false;
  } else if (name.indexOf('cache_flush_count') >= 0) {
    use = false;
  } else if (name.indexOf('cache_time_us') >= 0) {
    use = false;
  }
  return use;
};


/**
 * Find or create a chart panel in its selected graph view.
 * @param {string} id Unique chart id.
 * @param {string} title Visible chart title.
 * @param {string} targetId The id of the graph view container.
 * @return {Element} Chart content element.
 */
pagespeed.Graphs.prototype.initChart = function(id, title, targetId) {
  var chart = this.chartCache_[title];
  if (chart) return chart;
  var target = document.getElementById(targetId);
  if (target.textContent == 'Loading Charts...') target.textContent = '';
  var panel = document.createElement('section');
  panel.className = 'pagespeed-graphs-panel';
  var heading = document.createElement('div');
  heading.className = 'pagespeed-graphs-panel-heading';
  var titleNode = document.createElement('h2');
  titleNode.className = 'pagespeed-graphs-title';
  titleNode.textContent = title;
  heading.appendChild(titleNode);
  panel.appendChild(heading);
  chart = document.createElement('div');
  chart.className = 'pagespeed-graphs-chart';
  chart.id = id;
  panel.appendChild(chart);
  target.appendChild(panel);
  this.chartCache_[title] = chart;
  return chart;
};

/**
 * Draw a responsive horizontal bar chart from the latest statistic snapshot.
 * @param {string} prefix Statistics prefix.
 * @param {string} title Chart title.
 * @param {string} targetId Graph view id.
 */
pagespeed.Graphs.prototype.drawBarChart = function(prefix, title, targetId) {
  var id = 'pagespeed-graphs-' + prefix;
  var settingPrefix = prefix + '_';
  var chart = this.initChart(id, title, targetId);
  while (chart.firstChild) chart.removeChild(chart.firstChild);
  if (!this.psolMessages_.length) return;
  var messages = this.psolMessages_[this.psolMessages_.length - 1].messages;
  var rows = [];
  var total = 0;
  for (var i = 0; i < messages.length; i++) {
    if (!pagespeed.Graphs.screenData(settingPrefix, messages[i].name)) continue;
    var value = Number(messages[i].value);
    if (!isFinite(value)) continue;
    var caption = messages[i].name.substring(settingPrefix.length)
        .replace(/_/g, ' ');
    rows.push({name: caption, value: value});
    total += Math.max(0, value);
  }
  rows.sort(function(a, b) { return b.value - a.value; });
  if (!rows.length) {
    var empty = document.createElement('p');
    empty.className = 'pagespeed-graphs-empty';
    empty.textContent = 'No measurements have been recorded for this category yet.';
    chart.appendChild(empty);
    return;
  }
  var max = Math.max(0, rows[0].value);
  var ns = 'http://www.w3.org/2000/svg';
  var width = 760;
  var rowHeight = 40;
  var height = Math.max(116, rows.length * rowHeight + 24);
  var svg = document.createElementNS(ns, 'svg');
  svg.setAttribute('class', 'pagespeed-graphs-bars');
  svg.setAttribute('viewBox', '0 0 ' + width + ' ' + height);
  svg.setAttribute('role', 'img');
  svg.setAttribute('aria-label', title + ', ' + rows.length + ' measurements');
  for (var i = 0; i < rows.length; i++) {
    var y = 12 + i * rowHeight;
    var label = document.createElementNS(ns, 'text');
    label.setAttribute('x', '0'); label.setAttribute('y', y + 13);
    label.setAttribute('class', 'pagespeed-graphs-bar-label');
    label.textContent = rows[i].name;
    svg.appendChild(label);
    var track = document.createElementNS(ns, 'rect');
    track.setAttribute('x', '0'); track.setAttribute('y', y + 19);
    track.setAttribute('width', '570'); track.setAttribute('height', '8');
    track.setAttribute('rx', '4'); track.setAttribute('class', 'pagespeed-graphs-bar-track');
    svg.appendChild(track);
    var bar = document.createElementNS(ns, 'rect');
    var barWidth = max > 0 ? 570 * Math.max(0, rows[i].value) / max : 0;
    bar.setAttribute('x', '0'); bar.setAttribute('y', y + 19);
    bar.setAttribute('width', Math.max(barWidth, rows[i].value > 0 ? 2 : 0));
    bar.setAttribute('height', '8'); bar.setAttribute('rx', '4');
    bar.setAttribute('class', 'pagespeed-graphs-bar');
    var tip = document.createElementNS(ns, 'title');
    tip.textContent = rows[i].name + ': ' + rows[i].value;
    bar.appendChild(tip); svg.appendChild(bar);
    var value = document.createElementNS(ns, 'text');
    value.setAttribute('x', '590'); value.setAttribute('y', y + 26);
    value.setAttribute('class', 'pagespeed-graphs-bar-value');
    var share = total > 0 ? 100 * Math.max(0, rows[i].value) / total : 0;
    value.textContent = rows[i].value.toLocaleString() + '  ·  ' + share.toFixed(1) + '%';
    svg.appendChild(value);
  }
  chart.appendChild(svg);
};

/**
 * Draw a responsive SVG time-series chart.
 * @param {string} prefix Statistics prefix.
 * @param {string} title Chart title.
 * @param {string} targetId Graph view id.
 */
pagespeed.Graphs.prototype.drawHistoryChart = function(prefix, title, targetId) {
  var id = 'pagespeed-graphs-' + prefix;
  var settingPrefix = prefix + '_';
  var chart = this.initChart(id, title, targetId);
  while (chart.firstChild) chart.removeChild(chart.firstChild);
  var seriesNames = [];
  var seriesValues = [];
  for (var i = 0; i < this.psolMessages_.length; i++) {
    var sample = this.psolMessages_[i];
    var found = {};
    for (var j = 0; j < sample.messages.length; j++) {
      var message = sample.messages[j];
      if (!pagespeed.Graphs.screenData(settingPrefix, message.name)) continue;
      var name = message.name.substring(settingPrefix.length);
      if (seriesNames.indexOf(name) < 0 && seriesNames.length < 8) {
        seriesNames.push(name);
        seriesValues.push([]);
        for (var prior = 0; prior < i; prior++) {
          seriesValues[seriesValues.length - 1].push(0);
        }
      }
      var index = seriesNames.indexOf(name);
      if (index >= 0) found[name] = Number(message.value);
    }
    for (var j = 0; j < seriesNames.length; j++) {
      seriesValues[j].push(found[seriesNames[j]] || 0);
    }
  }
  if (!this.psolMessages_.length || !seriesNames.length) {
    var empty = document.createElement('p');
    empty.className = 'pagespeed-graphs-empty';
    empty.textContent = 'Waiting for enough statistics samples to draw a trend.';
    chart.appendChild(empty);
    return;
  }
  var ns = 'http://www.w3.org/2000/svg';
  var width = 760, height = 260, left = 66, right = 16, top = 16, bottom = 30;
  var plotWidth = width - left - right, plotHeight = height - top - bottom;
  var max = 0;
  for (var i = 0; i < seriesValues.length; i++) {
    for (var j = 0; j < seriesValues[i].length; j++) {
      max = Math.max(max, seriesValues[i][j]);
    }
  }
  max = Math.max(max * 1.12, 1);
  var svg = document.createElementNS(ns, 'svg');
  svg.setAttribute('class', 'pagespeed-graphs-lines');
  svg.setAttribute('viewBox', '0 0 ' + width + ' ' + height);
  svg.setAttribute('role', 'img');
  svg.setAttribute('aria-label', title + ' trends for the last 24 hours');
  for (var tick = 0; tick <= 4; tick++) {
    var y = top + plotHeight * tick / 4;
    var grid = document.createElementNS(ns, 'line');
    grid.setAttribute('x1', left); grid.setAttribute('x2', width - right);
    grid.setAttribute('y1', y); grid.setAttribute('y2', y);
    grid.setAttribute('class', 'pagespeed-graphs-gridline'); svg.appendChild(grid);
    var label = document.createElementNS(ns, 'text');
    label.setAttribute('x', left - 9); label.setAttribute('y', y + 4);
    label.setAttribute('text-anchor', 'end');
    label.setAttribute('class', 'pagespeed-graphs-axis-label');
    label.textContent = (max * (1 - tick / 4)).toLocaleString();
    svg.appendChild(label);
  }
  var colors = ['#39765e', '#c18a43', '#4b83a5', '#8a6c9e', '#d06d55', '#478f8b', '#687c9a', '#a37b55'];
  var legend = document.createElement('div');
  legend.className = 'pagespeed-graphs-legend';
  for (var i = 0; i < seriesNames.length; i++) {
    var path = '';
    for (var j = 0; j < seriesValues[i].length; j++) {
      var x = left + (seriesValues[i].length < 2 ? plotWidth / 2 :
          plotWidth * j / (seriesValues[i].length - 1));
      var y = top + plotHeight * (1 - seriesValues[i][j] / max);
      path += (j ? ' L ' : 'M ') + x.toFixed(2) + ' ' + y.toFixed(2);
    }
    var line = document.createElementNS(ns, 'path');
    line.setAttribute('d', path); line.setAttribute('fill', 'none');
    line.setAttribute('stroke', colors[i % colors.length]);
    line.setAttribute('stroke-width', '2.5'); line.setAttribute('stroke-linecap', 'round');
    line.setAttribute('stroke-linejoin', 'round');
    line.setAttribute('vector-effect', 'non-scaling-stroke');
    var lineTitle = document.createElementNS(ns, 'title');
    lineTitle.textContent = seriesNames[i].replace(/_/g, ' ');
    line.appendChild(lineTitle); svg.appendChild(line);
    var item = document.createElement('span');
    item.className = 'pagespeed-graphs-legend-item';
    item.style.color = colors[i % colors.length];
    item.textContent = seriesNames[i].replace(/_/g, ' ');
    legend.appendChild(item);
  }
  var firstTime = this.psolMessages_[0].timeReceived;
  var lastTime = this.psolMessages_[this.psolMessages_.length - 1].timeReceived;
  var firstLabel = document.createElementNS(ns, 'text');
  firstLabel.setAttribute('x', left); firstLabel.setAttribute('y', height - 6);
  firstLabel.setAttribute('class', 'pagespeed-graphs-axis-label');
  firstLabel.textContent = firstTime.toLocaleTimeString([], {hour: 'numeric', minute: '2-digit'});
  svg.appendChild(firstLabel);
  var lastLabel = document.createElementNS(ns, 'text');
  lastLabel.setAttribute('x', width - right); lastLabel.setAttribute('y', height - 6);
  lastLabel.setAttribute('text-anchor', 'end');
  lastLabel.setAttribute('class', 'pagespeed-graphs-axis-label');
  lastLabel.textContent = lastTime.toLocaleTimeString([], {hour: 'numeric', minute: '2-digit'});
  svg.appendChild(lastLabel);
  chart.appendChild(svg);
  chart.appendChild(legend);
};


/**
 * The frequency of auto-refresh. Default is once per 5 seconds.
 * @private {number}
 * @const
 */
pagespeed.Graphs.FREQUENCY_ = 5;


/**
 * The size limit to the data array. Default is one day.
 * @private {number}
 * @const
 */
pagespeed.Graphs.TIMERANGE_ = 24 * 60 * 60 /
                                  pagespeed.Graphs.FREQUENCY_;


/**
 * The Main entry to start processing.
 * @export
 */
pagespeed.Graphs.Start = function() {
  var graphsOnload = function() {
    var graphsObj = new pagespeed.Graphs();
    graphsObj.parseLocation();
    for (var i in pagespeed.Graphs.DisplayDiv) {
      goog.events.listen(
          document.getElementById(pagespeed.Graphs.DisplayMode[i]), 'click',
          goog.bind(graphsObj.show, graphsObj, pagespeed.Graphs.DisplayDiv[i]));
    }
    // IE6/7 don't support this event. Then the back button would not work
    // because no requests captured.
    goog.events.listen(window, 'hashchange',
                       goog.bind(graphsObj.parseLocation, graphsObj));

    goog.events.listen(document.getElementById('auto-refresh'), 'change',
                       goog.bind(graphsObj.toggleAutorefresh,
                                 graphsObj));
    // We call listen() here so this listener is added to the xhr only once.
    // If we call listen() inside performRefresh() method, we are adding a new
    // listener to the xhr every time it auto-refreshes, which would cause
    // fetchContent() being called multiple times. Users will see an obvious
    // delay because we draw the same charts multiple times in one refresh.
    goog.events.listen(
        graphsObj.xhr_, goog.net.EventType.COMPLETE,
        goog.bind(graphsObj.parseAjaxResponse, graphsObj));
    setInterval(goog.bind(graphsObj.performRefresh, graphsObj),
                pagespeed.Graphs.FREQUENCY_ * 1000);
    graphsObj.performRefresh();
  };
  goog.events.listen(window, 'load', graphsOnload);
};
