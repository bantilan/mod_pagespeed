/*
 * Copyright 2026 mod_pagespeed contributors.
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

/** @fileoverview Read-only active configuration dashboard. */

'use strict';

goog.provide('pagespeed.Configuration');

/** @constructor */
pagespeed.Configuration = function() {};

/** Initialize the read-only configuration presentation. @export */
pagespeed.Configuration.Start = function() {
  var source = document.getElementById('pagespeed-config-source');
  var root = document.getElementById('pagespeed-config-root');
  if (!source || !root) return;
  var model = pagespeed.Configuration.parse_(source.textContent);
  pagespeed.Configuration.render_(root, model);
  source.style.display = 'none';
};

/** @param {string} text @return {!Object} Parsed active settings. @private */
pagespeed.Configuration.parse_ = function(text) {
  var model = {
    version: 'Not reported', filters: [], options: [], invalidation: '',
    raw: text
  };
  var section = '';
  var lines = text.split('\n');
  for (var i = 0; i < lines.length; i++) {
    var line = lines[i];
    var trimmed = line.trim();
    var version = trimmed.match(/^Version:\s*([^:]+)/);
    if (version) model.version = version[1].trim();
    if (trimmed == 'Filters' || trimmed == 'Options' ||
        trimmed == 'Domain Lawyer') {
      section = trimmed;
      continue;
    }
    if (section == 'Filters' && trimmed) {
      var filter = trimmed.match(/^([a-zA-Z0-9_-]{1,4})\s+(.+)$/);
      if (filter) {
        model.filters.push({
          code: filter[1],
          name: filter[2].trim(),
          group: pagespeed.Configuration.filterGroup_(filter[2])
        });
      }
    } else if (section == 'Options' && trimmed) {
      var option = line.match(/^\s*(.*?)\s+\(([^)]+)\)\s{2,}(.*)$/);
      if (option) {
        model.options.push({
          name: option[1].trim(),
          alias: option[2].trim(),
          value: option[3].trim()
        });
      }
    } else if (section == 'Domain Lawyer' &&
               trimmed.indexOf('Invalidation Timestamp:') == 0) {
      model.invalidation = trimmed.substring(
          'Invalidation Timestamp:'.length).trim();
    }
  }
  return model;
};

/** @param {string} filter @return {string} @private */
pagespeed.Configuration.filterGroup_ = function(filter) {
  if (/css|style|flatten/i.test(filter)) return 'CSS';
  if (/javascript|analytics|script/i.test(filter)) return 'JavaScript';
  if (/image|gif|jpeg|png|webp|avif|resize|recompress/i.test(filter)) {
    return 'Images';
  }
  return 'Other';
};

/** @param {string} name @return {string} @private */
pagespeed.Configuration.description_ = function(name) {
  var descriptions = {
    AdminPath: 'Path used to access the PageSpeed administration console.',
    AnalyticsID: 'Analytics property identifier used by the analytics filter.',
    AvoidRenamingIntrospectiveJavascript: 'Avoid renaming JavaScript that inspects its own source.',
    BeaconUrl: 'Endpoint used by PageSpeed beacon requests.',
    CriticalImagesBeaconEnabled: 'Collect image-priority data from browser beacons.',
    EnableCachePurge: 'Allow authorized cache purge requests.',
    EnableRewriting: 'Enable PageSpeed resource rewriting.',
    FileCacheInodeLimit: 'Maximum number of inodes used by the file cache.',
    FileCachePath: 'Filesystem location used for the PageSpeed cache.',
    InPlaceSMaxAgeSec: 'Shared-cache lifetime for in-place rewritten resources.',
    LogDir: 'Directory for PageSpeed message and statistics logs.',
    RewriteLevel: 'Default set of rewrite filters applied to resources.',
    Statistics: 'Expose PageSpeed statistics to the admin interface.',
    StatisticsLogging: 'Persist statistics samples for graphs and history.',
    SupportNoScriptEnabled: 'Enable alternate output for clients without scripts.'
  };
  return descriptions[name] || 'Effective PageSpeed setting for ' +
      name.replace(/([a-z])([A-Z])/g, '$1 $2') + '.';
};

/** @param {!Element} root @param {!Object} model @private */
pagespeed.Configuration.render_ = function(root, model) {
  while (root.firstChild) root.removeChild(root.firstChild);
  var notice = document.createElement('section');
  notice.className = 'pagespeed-config-notice';
  notice.innerHTML = '<span class="pagespeed-config-notice-icon">i</span>' +
      '<div><strong>Read-only configuration</strong><p>These settings reflect the active PageSpeed configuration. ' +
      'To make changes, update your server configuration and reload the service.</p></div>';
  root.appendChild(notice);

  var optionMap = {};
  for (var i = 0; i < model.options.length; i++) {
    optionMap[model.options[i].name] = model.options[i].value;
  }
  var summary = document.createElement('div');
  summary.className = 'pagespeed-config-summary';
  var summaryItems = [
    ['Configuration version', model.version, 'Current PageSpeed settings format'],
    ['Rewrite level', optionMap['RewriteLevel'] || 'Not specified', 'Default active filter set'],
    ['Rewriting', pagespeed.Configuration.isEnabled_(optionMap['EnableRewriting']) ? 'Enabled' : 'Disabled', 'Resource optimization status'],
    ['Statistics', pagespeed.Configuration.isEnabled_(optionMap['Statistics']) ? 'Enabled' : 'Disabled', 'Admin metrics availability']
  ];
  for (var i = 0; i < summaryItems.length; i++) {
    var card = document.createElement('article');
    card.className = 'pagespeed-config-summary-card';
    var label = document.createElement('span');
    label.className = 'pagespeed-config-summary-label';
    label.textContent = summaryItems[i][0];
    var value = document.createElement('strong');
    value.className = 'pagespeed-config-summary-value';
    value.textContent = summaryItems[i][1];
    var note = document.createElement('span');
    note.className = 'pagespeed-config-summary-note';
    note.textContent = summaryItems[i][2];
    card.appendChild(label); card.appendChild(value); card.appendChild(note);
    summary.appendChild(card);
  }
  root.appendChild(summary);

  var columns = document.createElement('div');
  columns.className = 'pagespeed-config-columns';
  var filters = document.createElement('section');
  filters.className = 'pagespeed-config-card';
  filters.innerHTML = '<div class="pagespeed-config-section-heading"><div>' +
      '<h2>Active filters</h2><p>PageSpeed filters currently enabled.</p>' +
      '</div></div>';
  var filterHeading = filters.querySelector('.pagespeed-config-section-heading');
  var viewAll = document.createElement('button');
  viewAll.type = 'button';
  viewAll.className = 'pagespeed-config-view-all';
  viewAll.textContent = 'View full configuration';
  viewAll.setAttribute('aria-expanded', 'false');
  var groups = ['CSS', 'JavaScript', 'Images', 'Other'];
  for (var i = 0; i < groups.length; i++) {
    var groupFilters = [];
    for (var j = 0; j < model.filters.length; j++) {
      if (model.filters[j].group == groups[i]) groupFilters.push(model.filters[j]);
    }
    if (!groupFilters.length) continue;
    var group = document.createElement('div');
    group.className = 'pagespeed-config-filter-group pagespeed-config-filter-' +
        groups[i].toLowerCase().replace(' ', '-');
    var heading = document.createElement('div');
    heading.className = 'pagespeed-config-filter-heading';
    var title = document.createElement('strong');
    title.textContent = groups[i] + ' (' + groupFilters.length + ')';
    var badge = document.createElement('span');
    badge.className = 'pagespeed-config-enabled';
    badge.textContent = 'Enabled';
    heading.appendChild(title); heading.appendChild(badge);
    var list = document.createElement('ul');
    list.className = 'pagespeed-config-filter-list';
    var visibleCount = Math.min(4, groupFilters.length);
    for (var j = 0; j < groupFilters.length; j++) {
      var item = document.createElement('li');
      var filterName = document.createElement('span');
      filterName.className = 'pagespeed-config-filter-name';
      filterName.textContent = groupFilters[j].name;
      var filterCode = document.createElement('code');
      filterCode.className = 'pagespeed-config-filter-code';
      filterCode.textContent = groupFilters[j].code;
      filterCode.title = 'PageSpeed filter ID';
      item.appendChild(filterName);
      item.appendChild(filterCode);
      item.hidden = j >= visibleCount;
      list.appendChild(item);
    }
    group.appendChild(heading);
    group.appendChild(list);
    if (groupFilters.length > visibleCount) {
      var more = document.createElement('button');
      more.type = 'button';
      more.className = 'pagespeed-config-filter-more';
      more.textContent = 'Show ' + (groupFilters.length - visibleCount) +
          ' more';
      more.setAttribute('aria-expanded', 'false');
      more.addEventListener('click', function(items, button, count, limit) {
        return function() {
          var expanded = button.getAttribute('aria-expanded') == 'true';
          for (var k = 0; k < items.length; k++) {
            if (k >= limit) items[k].hidden = expanded;
          }
          button.setAttribute('aria-expanded', expanded ? 'false' : 'true');
          button.textContent = expanded ? 'Show ' + count + ' more' :
                                          'Show less';
        };
      }(list.children, more, groupFilters.length - visibleCount,
        visibleCount));
      group.appendChild(more);
    }
    filters.appendChild(group);
  }
  if (!model.filters.length) {
    var empty = document.createElement('p');
    empty.className = 'pagespeed-config-empty';
    empty.textContent = 'No active filters were listed.';
    filters.appendChild(empty);
  }
  var fullConfig = document.createElement('pre');
  fullConfig.className = 'pagespeed-config-full';
  fullConfig.hidden = true;
  fullConfig.textContent = model.raw;
  viewAll.addEventListener('click', function() {
    var showing = !fullConfig.hidden;
    fullConfig.hidden = showing;
    viewAll.textContent = showing ? 'View full configuration' :
                                    'Hide full configuration';
    viewAll.setAttribute('aria-expanded', showing ? 'false' : 'true');
  });
  filterHeading.appendChild(viewAll);
  filters.appendChild(fullConfig);
  columns.appendChild(filters);

  var optionsCard = document.createElement('section');
  optionsCard.className = 'pagespeed-config-card pagespeed-config-options-card';
  var optionsHeading = document.createElement('div');
  optionsHeading.className = 'pagespeed-config-section-heading';
  optionsHeading.innerHTML = '<div><h2>Options</h2><p>Effective values for key PageSpeed options.</p></div>';
  var toolbar = document.createElement('div');
  toolbar.className = 'pagespeed-config-options-toolbar';
  var search = document.createElement('input');
  search.type = 'search'; search.placeholder = 'Search options...';
  search.setAttribute('aria-label', 'Search configuration options');
  toolbar.appendChild(search);
  optionsHeading.appendChild(toolbar);
  optionsCard.appendChild(optionsHeading);
  var tableWrap = document.createElement('div');
  tableWrap.className = 'pagespeed-admin-table-wrap';
  var table = document.createElement('table');
  table.className = 'pagespeed-admin-data-table pagespeed-config-table';
  var head = table.createTHead().insertRow(0);
  ['Option', 'Value', 'Description', ''].forEach(function(text) {
    var cell = document.createElement('th'); cell.textContent = text; head.appendChild(cell);
  });
  var body = table.createTBody();
  var more = document.createElement('button');
  more.type = 'button'; more.className = 'pagespeed-config-more';
  more.setAttribute('aria-expanded', 'false');
  optionsCard.appendChild(tableWrap);
  optionsCard.appendChild(more);
  tableWrap.appendChild(table);
  var expanded = false;
  var renderOptions = function() {
    while (body.firstChild) body.removeChild(body.firstChild);
    var query = search.value.toLowerCase();
    var visible = [];
    for (var i = 0; i < model.options.length; i++) {
      var opt = model.options[i];
      if ((opt.name + ' ' + opt.alias + ' ' + opt.value + ' ' +
           pagespeed.Configuration.description_(opt.name)).toLowerCase().indexOf(query) >= 0) {
        visible.push(opt);
      }
    }
    var limit = query || expanded ? visible.length : Math.min(9, visible.length);
    for (var i = 0; i < limit; i++) {
      var opt = visible[i];
      var row = body.insertRow(-1);
      var name = row.insertCell(0);
      name.innerHTML = '<strong>' + pagespeed.Configuration.escape_(opt.name) +
          '</strong><small>(' + pagespeed.Configuration.escape_(opt.alias) + ')</small>';
      name.className = 'pagespeed-config-option-name';
      var value = row.insertCell(1);
      value.textContent = opt.value;
      if (/^(true|false)$/i.test(opt.value)) {
        value.innerHTML = '<span class="pagespeed-config-value-badge ' +
            (/^true$/i.test(opt.value) ? 'is-true' : 'is-false') + '">' +
            pagespeed.Configuration.escape_(opt.value) + '</span>';
      }
      var description = row.insertCell(2);
      description.textContent = pagespeed.Configuration.description_(opt.name);
      var copyCell = row.insertCell(3);
      var copy = document.createElement('button');
      copy.type = 'button'; copy.className = 'pagespeed-config-copy';
      copy.textContent = 'Copy'; copy.setAttribute('aria-label', 'Copy ' + opt.name + ' value');
      copy.addEventListener('click', function(valueToCopy, button) {
        return function() {
          pagespeed.Configuration.copy_(valueToCopy, button);
        };
      }(opt.value, copy));
      copyCell.appendChild(copy);
    }
    var hidden = visible.length - limit;
    more.style.display = hidden > 0 || expanded ? '' : 'none';
    more.textContent = expanded ? 'Show fewer options' :
        'Show ' + hidden + ' more option' + (hidden == 1 ? '' : 's');
    more.setAttribute('aria-expanded', expanded ? 'true' : 'false');
  };
  search.addEventListener('input', renderOptions);
  more.addEventListener('click', function() { expanded = !expanded; renderOptions(); });
  renderOptions();
  columns.appendChild(optionsCard);
  root.appendChild(columns);

  var domain = document.createElement('section');
  domain.className = 'pagespeed-config-card pagespeed-config-domain';
  domain.innerHTML = '<div class="pagespeed-config-section-heading"><div>' +
      '<h2>Domain Lawyer</h2><p>Domain-specific configuration details.</p></div></div>';
  var timestamp = document.createElement('p');
  timestamp.className = 'pagespeed-config-timestamp';
  timestamp.textContent = model.invalidation || 'No invalidation timestamp reported.';
  domain.appendChild(timestamp);
  root.appendChild(domain);

  var exportButton = document.createElement('button');
  exportButton.type = 'button'; exportButton.className = 'pagespeed-config-export';
  exportButton.textContent = 'Export Configuration';
  exportButton.addEventListener('click', function() {
    var blob = new Blob([document.getElementById('pagespeed-config-source').textContent],
                        {type: 'text/plain;charset=utf-8'});
    var url = URL.createObjectURL(blob);
    var link = document.createElement('a');
    link.href = url; link.download = 'pagespeed-configuration.txt';
    link.click(); URL.revokeObjectURL(url);
  });
  var heading = document.querySelector('.pagespeed-dashboard-heading');
  if (heading) heading.appendChild(exportButton);
};

/** @param {?string} value @return {boolean} @private */
pagespeed.Configuration.isEnabled_ = function(value) {
  return !!value && !/^(false|0|off|no)$/i.test(value);
};

/** @param {string} text @return {string} @private */
pagespeed.Configuration.escape_ = function(text) {
  return text.replace(/&/g, '&amp;').replace(/</g, '&lt;')
      .replace(/>/g, '&gt;').replace(/"/g, '&quot;').replace(/'/g, '&#39;');
};

/** Copy an option value. @param {string} value @param {!Element} button @private */
pagespeed.Configuration.copy_ = function(value, button) {
  var done = function() {
    button.textContent = 'Copied';
    window.setTimeout(function() { button.textContent = 'Copy'; }, 1200);
  };
  var clipboard = navigator['clipboard'];
  if (clipboard && clipboard['writeText']) {
    clipboard['writeText'](value).then(done, function() {});
  } else {
    var field = document.createElement('textarea');
    field.value = value; document.body.appendChild(field);
    field.select(); document.execCommand('copy');
    document.body.removeChild(field); done();
  }
};

pagespeed.Configuration.Start();
