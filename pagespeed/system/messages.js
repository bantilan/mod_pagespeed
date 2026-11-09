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
   * The option of reversing list.
   * @private {boolean}
   */
  this.reverse_ = false;

  /**
   * The option of filtering.
   * @private {string}
   */
  this.filter_ = '';

  /** @private {string} */
  this.severityFilter_ = 'all';

  /**
   * The option of auto-refresh.
   * @private {boolean}
   */
  this.autoRefresh_ = false;

  var wrapper = document.createElement('div');
  wrapper.style.overflow = 'hidden';
  wrapper.style.clear = 'both';

  var uiTable = document.createElement('div');
  uiTable.id = 'ui-div';
  uiTable.className = 'pagespeed-stat-toolbar';
  uiTable.innerHTML =
      '<label>Reverse order <input type="checkbox" id="reverse" ' +
      (this.reverse_ ? 'checked' : '') + '></label>' +
      '<label>Auto refresh <input type="checkbox" ' +
      'id="auto-refresh" ' + (this.autoRefresh_ ? 'checked' : '') +
      '></label><label class="pagespeed-stat-filter">Filter ' +
      '<input id="text-filter" type="search" placeholder="Search messages">' +
      '</label><label>Severity <select id="severity-filter">' +
      '<option value="all">All levels</option>' +
      '<option value="info">Info</option>' +
      '<option value="warning">Warning</option>' +
      '<option value="error">Error</option>' +
      '<option value="fatal">Fatal</option></select></label>';
  wrapper.appendChild(uiTable);

  var numElement = document.createElement('div');
  numElement.id = 'num';
  numElement.className = 'pagespeed-show-number';
  wrapper.appendChild(numElement);

  var logElement = document.getElementById('log');
  document.body.insertBefore(wrapper, logElement);

  this.updateMessageCount();
};


/**
 * Updates the option of reversing the messages list.
 */
pagespeed.Messages.prototype.toggleReverse = function() {
  this.reverse_ = !this.reverse_;
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
  this.severityFilter_ = element.value;
  this.update();
};


/**
 * Update the text showing the number of messages.
 * @param {number=} opt_num An optional specified number to show when
 *     filtering the messages.
 */
pagespeed.Messages.prototype.updateMessageCount = function(opt_num) {
  // The default number of messages is the length of the array when opt_num
  // is not specified.
  var total = (opt_num != undefined) ? opt_num : this.psolMessages_.length;
  if (total == this.numUnfilteredMessages_) {
    document.getElementById('num').textContent = 'Total entries: ' +
        total;
  } else {
    document.getElementById('num').textContent =
        'Visible entries: ' + total + '/' + this.numUnfilteredMessages_;
  }
};


/**
 * Reverses or filters messages according to the options.
 */
pagespeed.Messages.prototype.update = function() {
  var logElement = document.getElementById('log');
  var messages = goog.array.clone(this.psolMessages_);
  if (this.filter_) {
    for (var i = messages.length - 1; i >= 0; --i) {
      if (!goog.string.caseInsensitiveContains(messages[i].text,
                                                this.filter_)) {
        messages.splice(i, 1);
      }
    }
  }
  if (this.severityFilter_ != 'all') {
    for (var j = messages.length - 1; j >= 0; --j) {
      if (messages[j].severity != this.severityFilter_) {
        messages.splice(j, 1);
      }
    }
  }
  this.updateMessageCount(messages.length);
  if (this.reverse_) {
    messages.reverse();
  }
  pagespeed.Messages.renderEntries_(logElement, messages, this.reverse_);
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
pagespeed.Messages.renderEntries_ = function(logElement, entries, newestFirst) {
  if ((' ' + logElement.className + ' ').indexOf(
          ' pagespeed-messages-ready ') < 0) {
    logElement.className += ' pagespeed-messages-ready';
  }
  var groups = [];
  var byKey = {};
  for (var i = 0; i < entries.length; ++i) {
    var entry = entries[i];
    var match = entry.text.match(/^\[([^\]]+)\]\s+\[([^\]]+)\]\s*/);
    entry.timestamp = match ? match[1] : '';
    entry.body = match ? entry.text.substring(match[0].length) : entry.text;
    entry.body = entry.body.replace(/^\d+\s+/, '');
    var key = entry.severity + '|' + entry.body;
    if (!byKey[key]) {
      byKey[key] = {severity: entry.severity, entries: []};
      groups.push(byKey[key]);
    }
    byKey[key].entries.push(entry);
  }

  var fragment = document.createDocumentFragment();
  if (!groups.length) {
    var empty = document.createElement('p');
    empty.className = 'pagespeed-message-empty';
    empty.textContent = 'No messages match this filter.';
    fragment.appendChild(empty);
  }
  for (var j = 0; j < groups.length; ++j) {
    var group = groups[j];
    var latest = group.entries[newestFirst ? 0 : group.entries.length - 1];
    var card = document.createElement('article');
    card.className = 'pagespeed-message-card severity-' + group.severity;
    var meta = document.createElement('div');
    meta.className = 'pagespeed-message-meta';
    var tag = document.createElement('span');
    tag.className = 'pagespeed-message-tag';
    tag.textContent = group.severity;
    meta.appendChild(tag);
    var date = document.createElement('time');
    date.className = 'pagespeed-message-date';
    date.textContent = latest.timestamp || 'Timestamp unavailable';
    meta.appendChild(date);
    card.appendChild(meta);
    var heading = document.createElement('h2');
    heading.className = 'pagespeed-message-title';
    heading.textContent = latest.body || latest.text;
    card.appendChild(heading);

    if (group.entries.length > 1) {
      var details = document.createElement('details');
      details.className = 'pagespeed-message-occurrences';
      var summary = document.createElement('summary');
      summary.textContent = 'Show ' + group.entries.length + ' entries';
      details.appendChild(summary);
      var list = document.createElement('ol');
      for (var k = 0; k < group.entries.length; ++k) {
        var item = document.createElement('li');
        var itemDate = document.createElement('time');
        itemDate.textContent = group.entries[k].timestamp ||
                               'Timestamp unavailable';
        item.appendChild(itemDate);
        var itemText = document.createElement('p');
        itemText.textContent = group.entries[k].body || group.entries[k].text;
        item.appendChild(itemText);
        list.appendChild(item);
      }
      details.appendChild(list);
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
    this.updateMessageCount();
    document.getElementById('log').textContent = pagespeed.Messages.DUMP_ERROR_;
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
    this.updateMessageCount();
    document.getElementById('log').textContent =
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
    goog.events.listen(document.getElementById('severity-filter'), 'change',
                       goog.bind(messagesObj.setSeverityFilter, messagesObj,
                                 document.getElementById('severity-filter')));
    goog.events.listen(document.getElementById('reverse'), 'change',
                       goog.bind(messagesObj.toggleReverse, messagesObj));
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
