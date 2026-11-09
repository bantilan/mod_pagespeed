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
 * @fileoverview Code for adding reversing lists, auto-refresh and filtering
 * functionalities to message history page in admin site.
 *
 * @author oschaaf@we-amp.com (Otto van der Schaaf)
 * @author xqyin@google.com (XiaoQian Yin)
 */

goog.provide('pagespeed.Messages');

goog.require('goog.array');
goog.require('goog.events');
goog.require('goog.net.EventType');
goog.require('goog.net.XhrIo');
goog.require('goog.string');



/**
 * @constructor
 * @param {goog.testing.net.XhrIo=} opt_xhr
 *     Optional mock XmlHttpRequests handler for testing.
 */
pagespeed.Messages = function(opt_xhr) {
  /**
   * The XmlHttpRequests handler for auto-refresh. We pass a mock XhrIo
   * when testing. Default is a normal XhrIo.
   * @private {goog.net.XhrIo|goog.testing.net.XhrIo}
   */
  this.xhr_ = opt_xhr || new goog.net.XhrIo();

  /**
   * The messages to be shown on the page.
   * @private {!Array.<string>}
   */
  this.psolMessages_ = pagespeed.Messages.readEntries_(
      document.getElementById('log'));

  /**
   * The total number of unfiltered messages.
   * @private {number}
   */
  this.numUnfilteredMessages_ = this.psolMessages_.length;

  /**
   * The option of filtering.
   * @private {string}
   */
  this.filter_ = '';

  /** @private {string} */
  this.severityFilter_ = 'all';

  /** @private {boolean} */
  this.groupByMessage_ = true;

  /** @private {boolean} */
  this.newestFirst_ = true;

  /**
   * The option of auto-refresh.
   * @private {boolean}
   */
  this.autoRefresh_ = false;

  var wrapper = document.createElement('section');
  wrapper.id = 'message-history-controls';
  wrapper.className = 'pagespeed-message-controls';
  wrapper.innerHTML =
      '<div class="pagespeed-message-tools">' +
      '<label class="pagespeed-message-search-label">Search logs ' +
      '<input id="text-filter" type="search" ' +
      'placeholder="Search message or source"></label>' +
      '<label>Date from <input id="date-from" type="date"></label>' +
      '<label>Date to <input id="date-to" type="date"></label>' +
      '<label class="pagespeed-message-toggle">Group by message ' +
      '<input id="group-by-message" type="checkbox" checked></label>' +
      '<label class="pagespeed-message-toggle">Auto refresh ' +
      '<input id="auto-refresh" type="checkbox"></label></div>' +
      '<div class="pagespeed-message-filter-row">' +
      '<div class="pagespeed-message-levels" role="group" ' +
      'aria-label="Filter log severity">' +
      '<button type="button" data-level="all" class="active">All logs ' +
      '<span data-count="all">0</span></button>' +
      '<button type="button" data-level="error">Errors ' +
      '<span data-count="error">0</span></button>' +
      '<button type="button" data-level="warning">Warnings ' +
      '<span data-count="warning">0</span></button>' +
      '<button type="button" data-level="info">Info ' +
      '<span data-count="info">0</span></button>' +
      '<button type="button" data-level="fatal">Fatal ' +
      '<span data-count="fatal">0</span></button></div>' +
      '<label class="pagespeed-message-toggle">Newest first ' +
      '<input id="newest-first" type="checkbox" checked></label></div>' +
      '<p id="num" class="pagespeed-message-count" aria-live="polite"></p>';

  var logElement = document.getElementById('log');
  logElement.parentNode.insertBefore(wrapper, logElement);

  this.update();
};


/**
 * Updates the option of reversing the messages list.
 */
pagespeed.Messages.prototype.toggleOrder = function() {
  this.newestFirst_ = document.getElementById('newest-first').checked;
  this.update();
};


/**
 * Updates the option of auto-refresh.
 */
pagespeed.Messages.prototype.toggleAutorefresh = function() {
  this.autoRefresh_ = !this.autoRefresh_;
};


/**
 * Updates the option of messages filter.
 * @param {HTMLElement} element The HTML element of Filter.
 */
pagespeed.Messages.prototype.setFilter = function(element) {
  this.filter_ = element.value;
  this.update();
};


/** Updates the selected severity filter.
 * @param {!Element} element
 */
pagespeed.Messages.prototype.setSeverityFilter = function(element) {
  this.severityFilter_ = element.getAttribute('data-level');
  this.update();
};


pagespeed.Messages.prototype.setDateFilter = function() {
  this.dateFrom_ = document.getElementById('date-from').value;
  this.dateTo_ = document.getElementById('date-to').value;
  this.update();
};


pagespeed.Messages.prototype.toggleGrouping = function() {
  this.groupByMessage_ = document.getElementById('group-by-message').checked;
  this.update();
};


/**
 * Applies search, date, and severity filters, then renders grouped messages.
 */
pagespeed.Messages.prototype.update = function() {
  var logElement = document.getElementById('log');
  var matching = goog.array.clone(this.psolMessages_);
  var query = this.filter_.toLowerCase();
  var from = this.dateFrom_ ? new Date(this.dateFrom_ + 'T00:00:00').getTime() :
                              0;
  var to = this.dateTo_ ? new Date(this.dateTo_ + 'T23:59:59').getTime() :
                          Number.MAX_VALUE;

  for (var i = matching.length - 1; i >= 0; --i) {
    var timestamp = pagespeed.Messages.getTimestamp_(matching[i].text);
    var matchesText = !query ||
        goog.string.caseInsensitiveContains(matching[i].text, query);
    var matchesDate = !timestamp || (timestamp >= from && timestamp <= to);
    if (!matchesText || !matchesDate) {
      matching.splice(i, 1);
    }
  }

  var counts = {all: matching.length, error: 0, warning: 0, info: 0, fatal: 0};
  for (var j = 0; j < matching.length; ++j) {
    if (counts[matching[j].severity] != undefined) {
      ++counts[matching[j].severity];
    }
  }
  var buttons = document.querySelectorAll('.pagespeed-message-levels button');
  for (var k = 0; k < buttons.length; ++k) {
    var level = buttons[k].getAttribute('data-level');
    buttons[k].className = level == this.severityFilter_ ? 'active' : '';
    buttons[k].setAttribute('aria-pressed',
                            level == this.severityFilter_ ? 'true' : 'false');
    buttons[k].querySelector('[data-count]').textContent = counts[level];
  }
  if (this.severityFilter_ != 'all') {
    for (var m = matching.length - 1; m >= 0; --m) {
      if (matching[m].severity != this.severityFilter_) {
        matching.splice(m, 1);
      }
    }
  }
  var newestFirst = this.newestFirst_;
  matching.sort(function(a, b) {
    var delta = pagespeed.Messages.getTimestamp_(b.text) -
                pagespeed.Messages.getTimestamp_(a.text);
    return newestFirst ? delta : -delta;
  });

  var groups = pagespeed.Messages.groupEntries_(matching,
                                                 this.groupByMessage_);
  document.getElementById('num').textContent = 'Showing ' + groups.length +
      ' grouped logs (' + matching.length + ' total occurrences)';
  pagespeed.Messages.renderEntries_(logElement, groups);
};


/** @private */
pagespeed.Messages.readEntries_ = function(logElement) {
  var entries = [];
  var nodes = logElement.getElementsByClassName('pagespeed-message-entry');
  for (var i = 0; i < nodes.length; ++i) {
    var entry = {
      severity: nodes[i].getAttribute('data-severity') || 'info',
      text: nodes[i].textContent || ''
    };
    // SystemMessageHandler stores multiline messages as continuation lines.
    if (!/^\[[^\]]+\]\s+\[[^\]]+\]/.test(entry.text) && entries.length) {
      entries[entries.length - 1].text += '\n' + entry.text;
    } else {
      entries.push(entry);
    }
  }
  return entries;
};


/** @private */
pagespeed.Messages.getTimestamp_ = function(text) {
  var match = text.match(/^\[([^\]]+)\]/);
  var timestamp = match ? Date.parse(match[1]) : 0;
  return isNaN(timestamp) ? 0 : timestamp;
};


/** @private */
pagespeed.Messages.groupEntries_ = function(entries, groupByMessage) {
  var groups = [];
  var byKey = {};
  for (var i = 0; i < entries.length; ++i) {
    var entry = entries[i];
    var match = entry.text.match(/^\[([^\]]+)\]\s+\[([^\]]+)\]\s*/);
    entry.timestamp = match ? match[1] : '';
    entry.timestampMs = pagespeed.Messages.getTimestamp_(entry.text);
    entry.body = match ? entry.text.substring(match[0].length) : entry.text;
    // Drop the per-process id so identical messages from worker processes group.
    entry.body = entry.body.replace(/^\[\d+\]\s+/, '');
    var key = groupByMessage ? '$' + entry.severity + '|' + entry.body :
                              '$entry|' + i;
    if (!byKey[key]) {
      byKey[key] = {severity: entry.severity, body: entry.body, entries: []};
      groups.push(byKey[key]);
    }
    byKey[key].entries.push(entry);
  }
  return groups;
};


/** @private */
pagespeed.Messages.formatDate_ = function(timestamp) {
  return timestamp ? new Date(timestamp).toLocaleString() :
                     'Timestamp unavailable';
};


/** @private */
pagespeed.Messages.renderEntries_ = function(logElement, groups) {
  if ((' ' + logElement.className + ' ').indexOf(
          ' pagespeed-messages-ready ') < 0) {
    logElement.className += ' pagespeed-messages-ready';
  }
  var fragment = document.createDocumentFragment();
  if (!groups.length) {
    var empty = document.createElement('p');
    empty.className = 'pagespeed-message-empty';
    empty.textContent = 'No messages match these filters.';
    fragment.appendChild(empty);
  }
  for (var i = 0; i < groups.length; ++i) {
    var group = groups[i];
    var firstTime = Number.MAX_VALUE;
    var lastTime = 0;
    for (var j = 0; j < group.entries.length; ++j) {
      var occurrenceTime = group.entries[j].timestampMs;
      if (occurrenceTime) {
        firstTime = Math.min(firstTime, occurrenceTime);
        lastTime = Math.max(lastTime, occurrenceTime);
      }
    }
    if (firstTime == Number.MAX_VALUE) {
      firstTime = 0;
    }
    var card = document.createElement('article');
    card.className = 'pagespeed-message-card severity-' + group.severity;
    var head = document.createElement('div');
    head.className = 'pagespeed-message-card-head';
    var tag = document.createElement('span');
    tag.className = 'pagespeed-message-tag';
    tag.textContent = group.severity;
    head.appendChild(tag);
    var summary = document.createElement('div');
    summary.className = 'pagespeed-message-summary';
    var title = document.createElement('h2');
    title.className = 'pagespeed-message-title';
    title.textContent = group.body || '(empty message)';
    summary.appendChild(title);
    var occurrence = document.createElement('span');
    occurrence.className = 'pagespeed-message-occurrence-count';
    occurrence.textContent = group.entries.length + ' occurrence' +
        (group.entries.length == 1 ? '' : 's');
    summary.appendChild(occurrence);
    var range = document.createElement('span');
    range.className = 'pagespeed-message-date-range';
    range.textContent = 'First: ' + pagespeed.Messages.formatDate_(firstTime) +
        '  |  Last: ' + pagespeed.Messages.formatDate_(lastTime);
    summary.appendChild(range);
    head.appendChild(summary);
    card.appendChild(head);

    if (group.entries.length > 1) {
      var details = document.createElement('details');
      details.className = 'pagespeed-message-occurrences';
      var detailsSummary = document.createElement('summary');
      detailsSummary.textContent = 'Show all ' + group.entries.length +
          ' occurrences';
      details.appendChild(detailsSummary);
      var table = document.createElement('table');
      table.className = 'pagespeed-admin-data-table';
      table.innerHTML = '<thead><tr><th>Timestamp</th><th>Message</th>' +
                        '</tr></thead>';
      var tbody = document.createElement('tbody');
      for (var k = 0; k < group.entries.length; ++k) {
        var row = document.createElement('tr');
        var dateCell = document.createElement('td');
        dateCell.textContent = pagespeed.Messages.formatDate_(
            group.entries[k].timestampMs);
        var messageCell = document.createElement('td');
        messageCell.textContent = group.entries[k].body;
        row.appendChild(dateCell);
        row.appendChild(messageCell);
        tbody.appendChild(row);
      }
      table.appendChild(tbody);
      details.appendChild(table);
      card.appendChild(details);
    }
    fragment.appendChild(card);
  }
  while (logElement.firstChild) {
    logElement.removeChild(logElement.firstChild);
  }
  logElement.appendChild(fragment);
};


/**
  * The error message of dump failure.
  * @private {string}
  * @const
  */
pagespeed.Messages.DUMP_ERROR_ =
    'Failed to write messages to this page. ' +
    'Verify that MessageBufferSize is not set to 0 in pagespeed.conf.';


/**
 * Parses new messages from the server response.
 * @param {string} text The raw text content reponsed by server.
 * The expected format of text should be like this:
 * <html>
 *   <head>...</head>
 *   <body>
 *     <div style=...>...</div><hr>
 *     <div>...</div>
 *     <div id="log">...</div>
 *     <script type="text/javascript">...</script>
 *   </body>
 * </html>
 */
pagespeed.Messages.prototype.parseMessagesFromResponse = function(text) {
  var responseDoc = new DOMParser().parseFromString(text, 'text/html');
  var responseLog = responseDoc.getElementById('log');
  if (responseLog) {
    this.psolMessages_ = pagespeed.Messages.readEntries_(responseLog);
    this.numUnfilteredMessages_ = this.psolMessages_.length;
    this.update();
  } else {
    goog.array.clear(this.psolMessages_);
    this.numUnfilteredMessages_ = 0;
    this.update();
    document.querySelector('#log .pagespeed-message-empty').textContent =
        pagespeed.Messages.DUMP_ERROR_;
  }
};


/**
  * The error message of auto-refresh.
  * @private {string}
  * @const
  */
pagespeed.Messages.REFRESH_ERROR_ =
    'Sorry, the message history cannot be loaded. ' +
    'Please wait and try again later.';


/**
 * Refreshs the page by making requsts to server.
 */
pagespeed.Messages.prototype.autoRefresh = function() {
  if (this.autoRefresh_ && !this.xhr_.isActive()) {
    this.xhr_.send(document.location.href);
  }
};


/**
 * Parse the response sent by server.
 */
pagespeed.Messages.prototype.parseAjaxResponse = function() {
  if (this.xhr_.isSuccess()) {
    var newText = this.xhr_.getResponseText();
    this.parseMessagesFromResponse(newText);
  } else {
    console.log(this.xhr_.getLastError());
    goog.array.clear(this.psolMessages_);
    this.numUnfilteredMessages_ = 0;
    this.update();
    document.querySelector('#log .pagespeed-message-empty').textContent =
        pagespeed.Messages.REFRESH_ERROR_;
  }
};


/**
 * The Main entry to start processing.
 * @export
 */
pagespeed.Messages.Start = function() {
  var messagesOnload = function() {
    var messagesObj = new pagespeed.Messages();
    var filterElement = document.getElementById('text-filter');
    goog.events.listen(
        filterElement, 'keyup', goog.bind(messagesObj.setFilter,
                                          messagesObj, filterElement));
    var levelButtons = document.querySelectorAll(
        '.pagespeed-message-levels button');
    for (var i = 0; i < levelButtons.length; ++i) {
      goog.events.listen(levelButtons[i], 'click',
                         goog.bind(messagesObj.setSeverityFilter,
                                   messagesObj, levelButtons[i]));
    }
    goog.events.listen(document.getElementById('date-from'), 'change',
                       goog.bind(messagesObj.setDateFilter, messagesObj));
    goog.events.listen(document.getElementById('date-to'), 'change',
                       goog.bind(messagesObj.setDateFilter, messagesObj));
    goog.events.listen(document.getElementById('group-by-message'), 'change',
                       goog.bind(messagesObj.toggleGrouping, messagesObj));
    goog.events.listen(document.getElementById('newest-first'), 'change',
                       goog.bind(messagesObj.toggleOrder, messagesObj));
    goog.events.listen(document.getElementById('auto-refresh'), 'change',
                       goog.bind(messagesObj.toggleAutorefresh, messagesObj));
    goog.events.listen(messagesObj.xhr_, goog.net.EventType.COMPLETE,
                       goog.bind(messagesObj.parseAjaxResponse, messagesObj));
    setInterval(goog.bind(messagesObj.autoRefresh, messagesObj), 5000);
  };
  if (document.readyState == 'complete' ||
      document.readyState == 'interactive') {
    messagesOnload();
  } else {
    goog.events.listen(window, 'load', messagesOnload);
  }
};
