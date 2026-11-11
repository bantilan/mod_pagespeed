/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * 
 *   http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */


#include "pagespeed/kernel/base/statistics.h"

#include <limits>
#include <map>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "pagespeed/kernel/base/abstract_mutex.h"
#include "pagespeed/kernel/base/string.h"
#include "pagespeed/kernel/base/string_util.h"
#include "pagespeed/kernel/base/string_writer.h"
#include "pagespeed/kernel/base/writer.h"

namespace net_instaweb {

const char Statistics::kDefaultGroup[] = "Statistics";

class MessageHandler;

Variable::~Variable() {
}

UpDownCounter::~UpDownCounter() {
}

int64 UpDownCounter::SetReturningPreviousValue(int64 value) {
  int64 previous_value = Get();
  Set(value);
  return previous_value;
}

MutexedScalar::~MutexedScalar() {
}

int64 MutexedScalar::Get() const {
  if (mutex() != NULL) {
    ScopedMutex hold_lock(mutex());
    return GetLockHeld();
  } else {
    return -1;
  }
}

void MutexedScalar::Set(int64 new_value) {
  if (mutex() != NULL) {
    ScopedMutex hold_lock(mutex());
    SetLockHeld(new_value);
  }
}

int64 MutexedScalar::SetReturningPreviousValue(int64 new_value) {
  if (mutex() != NULL) {
    ScopedMutex hold_lock(mutex());
    return SetReturningPreviousValueLockHeld(new_value);
  } else {
    return -1;
  }
}

int64 MutexedScalar::AddHelper(int64 delta) {
  if (mutex() != NULL) {
    ScopedMutex hold_lock(mutex());
    return AddLockHeld(delta);
  } else {
    return -1;
  }
}

void MutexedScalar::SetLockHeld(int64 new_value) {
  SetReturningPreviousValueLockHeld(new_value);
}

int64 MutexedScalar::AddLockHeld(int64 delta) {
  int64 value = GetLockHeld() + delta;
  SetLockHeld(value);
  return value;
}

Histogram::~Histogram() {
}

CountHistogram::CountHistogram(AbstractMutex* mutex)
    : mutex_(mutex), count_(0) {}

CountHistogram::~CountHistogram() {
}

TimedVariable::~TimedVariable() {
}

FakeTimedVariable::FakeTimedVariable(StringPiece name, Statistics* stats)
    : var_(stats->AddVariable(name)) {
}

FakeTimedVariable::~FakeTimedVariable() {
}

void Histogram::WriteRawHistogramData(Writer* writer, MessageHandler* handler) {
  const char row_format[] =
      "<tr class=\"%s\"><td class=\"pagespeed-histogram-range\">%s&ndash;%s</td>"
      "<td class=\"pagespeed-histogram-count\">%.0f</td>"
      "<td class=\"pagespeed-histogram-share\">%.1f%%</td>"
      "<td class=\"pagespeed-histogram-cumulative\">%.1f%%</td>"
      "<td class=\"pagespeed-histogram-distribution\">%s</td></tr>";
  double count = CountInternal();
  double perc = 0;
  double cumulative_perc = 0;
  // Write prefix of the table.
  writer->Write(
      "<div class='pagespeed-admin-table-wrap'><table class='"
      "pagespeed-admin-data-table pagespeed-histogram-buckets'>"
      "<thead><tr><th>Range</th><th>Samples</th><th>Share</th>"
      "<th>Cumulative</th><th>Distribution</th></tr></thead><tbody>",
      handler);
  for (int i = 0, n = NumBuckets(); i < n; ++i) {
    double value = BucketCount(i);
    if (value == 0) continue;
    double lower_bound = BucketStart(i);
    double upper_bound = BucketLimit(i);

    GoogleString lower_bound_string = StringPrintf("%.0f", lower_bound);
    if (lower_bound == -std::numeric_limits<double>::infinity()) {
      lower_bound_string = "-&infin;";
    }
    GoogleString upper_bound_string = StringPrintf("%.0f", upper_bound);
    if (upper_bound == std::numeric_limits<double>::infinity()) {
      upper_bound_string = "&infin;";
    }

    perc = count > 0 ? value * 100 / count : 0;
    cumulative_perc += perc;
    GoogleString distribution = value > 0
        ? StringPrintf("<span class=\"pagespeed-histogram-bar\" "
                       "style=\"width: %.1f%%\"></span>", perc)
        : "&mdash;";
    GoogleString output = StringPrintf(
        row_format, "", lower_bound_string.c_str(), upper_bound_string.c_str(),
        value, perc, cumulative_perc, distribution.c_str());
    writer->Write(output, handler);
  }
  // Write suffix of the table.
  writer->Write("</tbody></table></div>", handler);
}

void Histogram::Render(int index, Writer* writer, MessageHandler* handler) {
  writer->Write(StringPrintf("<div id='hist_%d' style='display:none'>", index),
                handler);

  // Don't hold a lock while calling the writer, as this can deadlock if
  // the writer itself winds up invoking pagespeed, causing the histogram
  // to be locked.  So buffer each histogram and release the lock before
  // passing it to writer.
  GoogleString buf;
  {
    ScopedMutex hold(lock());
    StringWriter string_writer(&buf);
    WriteRawHistogramData(&string_writer, handler);
  }

  writer->Write(buf, handler);
  writer->Write("</div>\n", handler);
}

Statistics::~Statistics() {
}

UpDownCounter* Statistics::AddGlobalUpDownCounter(const StringPiece& name) {
  return AddUpDownCounter(name);
}

namespace {

const char kHistogramProlog[] =
    "<div class='pagespeed-admin-table-wrap'>\n"
    "  <table class='pagespeed-admin-data-table pagespeed-histogram-table'>\n"
    "    <thead><tr>\n"
    "      <th>Histogram</th>\n"
    "      <th>Count</th>\n"
    "      <th>Average</th>\n"
    "      <th>Std. Dev.</th>\n"
    "      <th>Min</th>\n"
    "      <th>Median</th>\n"
    "      <th>Max</th>\n"
    "      <th>90th</th>\n"
    "      <th>95th</th>\n"
    "      <th>99th</th>\n"
    "    </tr></thead><tbody>\n";

const char kHistogramRowFormat[] =
    "      <tr id='hist_row_%d'>\n"
    "        <td><button type='button' class='pagespeed-histogram-select'\n"
    "                   onclick='setHistogram(%d)' aria-pressed='%s'>%s</button></td>\n"
    "        <td>%.0f</td><td>%.1f</td><td>%.1f</td>\n"  // count, avg, stddev
    "        <td>%.0f</td><td>%.0f</td><td>%.0f</td>\n"  // min, median, max
    "        <td>%.0f</td><td>%.0f</td><td>%.0f</td>\n"  // 90%, 95%, 99%
    "     </tr>\n";

const char kHistogramEpilog[] =
    "    </tbody>\n"
    "  </table>\n"
    "</div>\n";

const char kHistogramScript[] =
    "<script>\n"
    "  var currentHistogram = -1;\n"
    "  function setHistogramBase(id) {\n"
    "    var div = document.getElementById('hist_' + currentHistogram);\n"
    "    if (div) {\n"
    "      div.style.display = 'none';\n"
    "    }\n"
    "    div = document.getElementById('hist_' + id);\n"
    "    if (div) {\n"
    "      div.style.display = '';\n"
    "    }\n"
    "    var row = document.getElementById('hist_row_' + currentHistogram);\n"
    "    if (row) {\n"
    "      row.className = '';\n"
    "      var button = row.querySelector('button');\n"
    "      if (button) button.setAttribute('aria-pressed', 'false');\n"
    "    }\n"
    "    row = document.getElementById('hist_row_' + id);\n"
    "    if (row) {\n"
    "      row.className = 'pagespeed-histogram-selected';\n"
    "      var button = row.querySelector('button');\n"
    "      if (button) button.setAttribute('aria-pressed', 'true');\n"
    "    }\n"
    "    currentHistogram = id;\n"
    "  }\n"
    "  function histogramNumber(value) {\n"
    "    var number = parseFloat(String(value).replace(/,/g, ''));\n"
    "    return isNaN(number) ? 0 : number;\n"
    "  }\n"
    "  function histogramFormat(value) {\n"
    "    var parts = String(value).split('.');\n"
    "    parts[0] = parts[0].replace(/\\B(?=(\\d{3})+(?!\\d))/g, ',');\n"
    "    return parts.join('.');\n"
    "  }\n"
    "  function histogramAddMetric(container, label, value) {\n"
    "    var card = document.createElement('div');\n"
    "    card.className = 'pagespeed-histogram-metric';\n"
    "    var title = document.createElement('span');\n"
    "    title.className = 'pagespeed-histogram-metric-label';\n"
    "    title.textContent = label;\n"
    "    var metric = document.createElement('strong');\n"
    "    metric.className = 'pagespeed-histogram-metric-value';\n"
    "    metric.textContent = value;\n"
    "    card.appendChild(title); card.appendChild(metric);\n"
    "    container.appendChild(card);\n"
    "  }\n"
    "  function histogramRows() {\n"
    "    var table = document.querySelector('#pagespeed-histograms "
    "table.pagespeed-histogram-table');\n"
    "    return table ? table.tBodies[0].rows : [];\n"
    "  }\n"
    "  function exportHistogramCsv() {\n"
    "    var table = document.querySelector('#pagespeed-histograms "
    "table.pagespeed-histogram-table');\n"
    "    if (!table) return;\n"
    "    var lines = [];\n"
    "    for (var i = 0; i < table.rows.length; i++) {\n"
    "      var values = [];\n"
    "      for (var j = 0; j < table.rows[i].cells.length; j++) {\n"
    "        var value = table.rows[i].cells[j].textContent.replace(/\"/g, '\"\"');\n"
    "        values.push('\"' + value + '\"');\n"
    "      }\n"
    "      lines.push(values.join(','));\n"
    "    }\n"
    "    var blob = new Blob([lines.join('\\r\\n')], {type: 'text/csv;charset=utf-8'});\n"
    "    var url = window.URL.createObjectURL(blob);\n"
    "    var link = document.createElement('a'); link.href = url;\n"
    "    link.download = 'pagespeed-histograms.csv';\n"
    "    document.body.appendChild(link); link.click(); document.body.removeChild(link);\n"
    "    window.setTimeout(function() { window.URL.revokeObjectURL(url); }, 1000);\n"
    "  }\n"
    "  function histogramRange(row) {\n"
    "    return row.cells[0].textContent.replace(/\\s+/g, ' ').trim();\n"
    "  }\n"
    "  function enhanceHistogramDetail(id) {\n"
    "    var detail = document.getElementById('hist_' + id);\n"
    "    var summaryRow = document.getElementById('hist_row_' + id);\n"
    "    if (!detail || !summaryRow) return;\n"
    "    if (detail.getAttribute('data-enhanced') === 'true') {\n"
    "      var oldHeading = detail.querySelector('.pagespeed-histogram-detail-heading');\n"
    "      if (oldHeading) oldHeading.firstChild.textContent = "
    "summaryRow.cells[0].textContent;\n"
    "      return;\n"
    "    }\n"
    "    detail.setAttribute('data-enhanced', 'true');\n"
    "    detail.className = 'pagespeed-histogram-detail';\n"
    "    var heading = document.createElement('h3');\n"
    "    heading.className = 'pagespeed-histogram-detail-heading';\n"
    "    heading.textContent = summaryRow.cells[0].textContent;\n"
    "    var subtitle = document.createElement('small');\n"
    "    subtitle.textContent = 'Bucket distribution for this operation';\n"
    "    heading.appendChild(subtitle);\n"
    "    var grid = document.createElement('div');\n"
    "    grid.className = 'pagespeed-histogram-detail-grid';\n"
    "    var bucketPanel = document.createElement('section');\n"
    "    bucketPanel.className = 'pagespeed-histogram-bucket-panel';\n"
    "    var bucketTitle = document.createElement('h4');\n"
    "    bucketTitle.className = 'pagespeed-histogram-panel-title';\n"
    "    bucketTitle.textContent = 'Bucket distribution';\n"
    "    bucketPanel.appendChild(bucketTitle);\n"
    "    var bucketWrap = detail.querySelector('.pagespeed-admin-table-wrap');\n"
    "    if (bucketWrap) bucketPanel.appendChild(bucketWrap);\n"
    "    var side = document.createElement('div');\n"
    "    side.className = 'pagespeed-histogram-side';\n"
    "    var sharePanel = document.createElement('section');\n"
    "    sharePanel.className = 'pagespeed-histogram-share-panel';\n"
    "    var shareTitle = document.createElement('h4');\n"
    "    shareTitle.className = 'pagespeed-histogram-panel-title';\n"
    "    shareTitle.textContent = 'Distribution share';\n"
    "    var shareLayout = document.createElement('div');\n"
    "    shareLayout.className = 'pagespeed-histogram-share-layout';\n"
    "    var donut = document.createElement('div');\n"
    "    donut.className = 'pagespeed-histogram-donut';\n"
    "    var center = document.createElement('div');\n"
    "    center.className = 'pagespeed-histogram-donut-center';\n"
    "    var legend = document.createElement('div');\n"
    "    legend.className = 'pagespeed-histogram-legend';\n"
    "    shareLayout.appendChild(donut); shareLayout.appendChild(legend);\n"
    "    sharePanel.appendChild(shareTitle); sharePanel.appendChild(shareLayout);\n"
    "    var insightPanel = document.createElement('section');\n"
    "    insightPanel.className = 'pagespeed-histogram-insights';\n"
    "    var insightTitle = document.createElement('h4');\n"
    "    insightTitle.className = 'pagespeed-histogram-panel-title';\n"
    "    insightTitle.textContent = 'Key insights';\n"
    "    var insightList = document.createElement('div');\n"
    "    insightList.className = 'pagespeed-histogram-insight-list';\n"
    "    insightPanel.appendChild(insightTitle); insightPanel.appendChild(insightList);\n"
    "    side.appendChild(sharePanel); side.appendChild(insightPanel);\n"
    "    grid.appendChild(bucketPanel); grid.appendChild(side);\n"
    "    detail.insertBefore(heading, detail.firstChild);\n"
    "    detail.appendChild(grid);\n"
    "    var bucketTable = detail.querySelector('.pagespeed-histogram-buckets');\n"
    "    var bucketRows = bucketTable ? bucketTable.tBodies[0].rows : [];\n"
    "    var histogramName = summaryRow.cells[0].textContent;\n"
    "    if (bucketTable && /latency/i.test(histogramName)) {\n"
    "      bucketTable.tHead.rows[0].cells[0].textContent = 'Latency range';\n"
    "      bucketTable.tHead.rows[0].cells[1].textContent = 'Requests';\n"
    "      var unit = /_ms$/i.test(histogramName) ? ' ms' :\n"
    "          (/_us$/i.test(histogramName) ? ' us' : '');\n"
    "      if (unit) for (var r = 0; r < bucketRows.length; r++)\n"
    "        bucketRows[r].cells[0].textContent += unit;\n"
    "    }\n"
    "    var colors = ['#39765e', '#6a9c7b', '#d69a45', '#92b7a0', '#b8cbbd'];\n"
    "    var stops = []; var cursor = 0; var total = 0; var common = null;\n"
    "    for (var i = 0; i < bucketRows.length; i++) {\n"
    "      var cells = bucketRows[i].cells;\n"
    "      var share = histogramNumber(cells[2].textContent);\n"
    "      var count = histogramNumber(cells[1].textContent);\n"
    "      var range = histogramRange(bucketRows[i]);\n"
    "      if (!common || share > common.share) common = {share: share, range: range};\n"
    "      total += count;\n"
    "      if (share > 0) {\n"
    "        stops.push(colors[i % colors.length] + ' ' + cursor + '% ' + (cursor + share) + '%');\n"
    "        cursor += share;\n"
    "      }\n"
    "      if (i < 4) {\n"
    "        var item = document.createElement('div');\n"
    "        item.className = 'pagespeed-histogram-legend-item';\n"
    "        var swatch = document.createElement('span');\n"
    "        swatch.className = 'pagespeed-histogram-legend-swatch';\n"
    "        swatch.style.backgroundColor = colors[i % colors.length];\n"
    "        var label = document.createElement('span');\n"
    "        label.className = 'pagespeed-histogram-legend-range'; label.textContent = range;\n"
    "        var percent = document.createElement('strong');\n"
    "        percent.textContent = histogramFormat(share.toFixed(1)) + '%';\n"
    "        item.appendChild(swatch); item.appendChild(label); item.appendChild(percent);\n"
    "        legend.appendChild(item);\n"
    "      }\n"
    "    }\n"
    "    donut.style.background = stops.length ? 'conic-gradient(' + stops.join(',') + ')' : '#e8efea';\n"
    "    center.textContent = histogramFormat(String(Math.round(total)));\n"
    "    var centerLabel = document.createElement('small'); centerLabel.textContent = 'samples';\n"
    "    center.appendChild(centerLabel); donut.appendChild(center);\n"
    "    var insights = [];\n"
    "    if (common) insights.push(['Most common range', common.range + ' - ' + common.share.toFixed(1) + '% of samples']);\n"
    "    insights.push(['95th percentile', summaryRow.cells[8].textContent + ' for this metric']);\n"
    "    insights.push(['Spread', 'Std. dev. ' + summaryRow.cells[3].textContent + ' - ' + bucketRows.length + ' populated ranges']);\n"
    "    for (var j = 0; j < insights.length; j++) {\n"
    "      var insight = document.createElement('div'); insight.className = 'pagespeed-histogram-insight';\n"
    "      var insightLabel = document.createElement('strong'); insightLabel.textContent = insights[j][0];\n"
    "      insight.appendChild(insightLabel); insight.appendChild(document.createTextNode(insights[j][1]));\n"
    "      insightList.appendChild(insight);\n"
    "    }\n"
    "  }\n"
    "  function updateHistogramSummary(id) {\n"
    "    var rows = histogramRows(); var visible = 0; var samples = 0;\n"
    "    var selected = document.getElementById('hist_row_' + id);\n"
    "    for (var i = 0; i < rows.length; i++) {\n"
    "      if (rows[i].style.display === 'none') continue;\n"
    "      visible++; samples += histogramNumber(rows[i].cells[1].textContent);\n"
    "    }\n"
    "    var cards = document.querySelectorAll('.pagespeed-histogram-metric-value');\n"
    "    if (cards.length >= 4) {\n"
    "      cards[0].textContent = histogramFormat(String(visible));\n"
    "      cards[1].textContent = histogramFormat(String(Math.round(samples)));\n"
    "      cards[2].textContent = selected ? selected.cells[8].textContent : '-';\n"
    "      cards[3].textContent = selected ? selected.cells[6].textContent : '-';\n"
    "    }\n"
    "    enhanceHistogramDetail(id);\n"
    "  }\n"
    "  function setHistogram(id) { setHistogramBase(id); updateHistogramSummary(id); }\n"
    "  (function enhanceHistogramPage() {\n"
    "    var root = document.getElementById('pagespeed-histograms');\n"
    "    var table = root ? root.querySelector('.pagespeed-histogram-table') : null;\n"
    "    if (!root || !table) {\n"
    "      var empty = root ? root.querySelector('em') : null;\n"
    "      if (empty) empty.className = 'pagespeed-histogram-empty';\n"
    "      return;\n"
    "    }\n"
    "    var rows = table.tBodies[0].rows; var samples = 0;\n"
    "    for (var i = 0; i < rows.length; i++) samples += histogramNumber(rows[i].cells[1].textContent);\n"
    "    var summary = document.createElement('div'); summary.className = 'pagespeed-histogram-summary';\n"
    "    histogramAddMetric(summary, 'Visible histograms', rows.length);\n"
    "    histogramAddMetric(summary, 'Metric samples', histogramFormat(String(Math.round(samples))));\n"
    "    histogramAddMetric(summary, 'Selected P95', rows.length ? rows[0].cells[8].textContent : '-');\n"
    "    histogramAddMetric(summary, 'Selected maximum', rows.length ? rows[0].cells[6].textContent : '-');\n"
    "    var toolbar = document.createElement('div'); toolbar.className = 'pagespeed-histogram-toolbar';\n"
    "    var copy = document.createElement('div');\n"
    "    var title = document.createElement('h2'); title.textContent = 'Histogram details';\n"
    "    var description = document.createElement('p'); description.textContent = 'Counts are cumulative since startup. Select a metric to inspect its distribution.';\n"
    "    copy.appendChild(title); copy.appendChild(description);\n"
    "    var search = document.createElement('input'); search.type = 'search';\n"
    "    search.className = 'pagespeed-histogram-search'; search.placeholder = 'Filter histograms...';\n"
    "    search.setAttribute('aria-label', 'Filter histograms');\n"
    "    var actions = document.createElement('div'); actions.className = 'pagespeed-histogram-actions';\n"
    "    var exportButton = document.createElement('button'); exportButton.type = 'button';\n"
    "    exportButton.className = 'pagespeed-histogram-export'; exportButton.textContent = 'Export CSV';\n"
    "    exportButton.addEventListener('click', exportHistogramCsv);\n"
    "    actions.appendChild(search); actions.appendChild(exportButton);\n"
    "    toolbar.appendChild(copy); toolbar.appendChild(actions);\n"
    "    var tableWrap = table.parentNode; tableWrap.className += ' pagespeed-histogram-table-card';\n"
    "    root.insertBefore(summary, tableWrap); root.insertBefore(toolbar, tableWrap);\n"
    "    search.addEventListener('input', function() {\n"
    "      var term = search.value.toLowerCase(); var first = null;\n"
    "      var allRows = table.tBodies[0].rows;\n"
    "      for (var j = 0; j < allRows.length; j++) {\n"
    "        var match = allRows[j].cells[0].textContent.toLowerCase().indexOf(term) !== -1;\n"
    "        allRows[j].style.display = match ? '' : 'none'; if (match && !first) first = allRows[j];\n"
    "      }\n"
    "      if (first) setHistogram(first.id.replace('hist_row_', ''));\n"
    "      else {\n"
    "        var activeDetail = document.getElementById('hist_' + currentHistogram);\n"
    "        if (activeDetail) activeDetail.style.display = 'none';\n"
    "        currentHistogram = -1; updateHistogramSummary(-1);\n"
    "      }\n"
    "    });\n"
    "    setHistogram(0);\n"
    "  })();\n"
    "</script>\n";

}  // namespace

void Statistics::RenderHistograms(Writer* writer, MessageHandler* handler) {
  StringVector hist_names = HistogramNames();  // includes empty ones.
  StringVector populated_histogram_names;
  std::vector<Histogram*> populated_histograms;

  // Find non-empty histograms.  Note that when the server first comes
  // up, there won't be any data in the histograms because there is no
  // traffic.  Other histograms may never be populated depending on
  // mod_pagespeed settings.  We pre-scan the histograms, capturing a
  // snapshot of the non-empty ones, because a histogram might become
  // non-empty asynchronously between the next two loops, and that
  // would skew indexing.
  for (int i = 0, n = hist_names.size(); i < n; ++i) {
    Histogram* hist = FindHistogram(hist_names[i]);

    // Exclude histograms with zero count.
    if (hist->Count() != 0) {
      populated_histograms.push_back(hist);
      populated_histogram_names.push_back(hist_names[i]);
    }
  }

  writer->Write("<hr/>", handler);

  // Write table data for each histogram.
  if (populated_histograms.empty()) {
    writer->Write("<em>No histogram data yet.  Refresh once there is "
                  "traffic.</em>", handler);
  } else {
    // Write the table header for all histograms.
    writer->Write(StringPiece(kHistogramProlog,
                              STATIC_STRLEN(kHistogramProlog)),
                  handler);

    // Write a row of the table data for each non-empty histogram.
    CHECK_EQ(populated_histogram_names.size(), populated_histograms.size());
    for (int i = 0, n = populated_histograms.size(); i < n; ++i) {
      Histogram* hist = populated_histograms[i];
      GoogleString row = hist->HtmlTableRow(populated_histogram_names[i], i);
      writer->Write(row, handler);
    }
    writer->Write(StringPiece(kHistogramEpilog,
                              STATIC_STRLEN(kHistogramEpilog)),
                  handler);

    // Render the non-empty histograms.
    for (int i = 0, n = populated_histograms.size(); i < n; ++i) {
      populated_histograms[i]->Render(i, writer, handler);
    }

    // Write the JavaScript to display the histograms and highlight the row
    // when selected.
    writer->Write(StringPiece(kHistogramScript,
                              STATIC_STRLEN(kHistogramScript)),
                  handler);
  }
  writer->Write("<hr/>\n", handler);
}

GoogleString Histogram::HtmlTableRow(const GoogleString& title, int index) {
  ScopedMutex hold(lock());
  return StringPrintf(
      kHistogramRowFormat,
      index,
      index,
      (index == 0) ? "true" : "false",
      title.c_str(),
      CountInternal(),
      AverageInternal(),
      StandardDeviationInternal(),
      MinimumInternal(),
      PercentileInternal(50),
      MaximumInternal(),
      PercentileInternal(90),
      PercentileInternal(95),
      PercentileInternal(99));
}

void Statistics::RenderTimedVariables(Writer* writer,
                                      MessageHandler* message_handler) {
  TimedVariable* timedvar = NULL;
  const GoogleString end("</table>\n<td>\n<td>\n");
  std::map<GoogleString, StringVector> group_map = TimedVariableMap();
  std::map<GoogleString, StringVector>::const_iterator p;
  // Export statistics in each group in one table.
  for (p = group_map.begin(); p != group_map.end(); ++p) {
    // Write table header for each group.
    const GoogleString begin = StrCat(
        "<p><table bgcolor=#eeeeff width=100%%>",
        "<tr align=center><td><font size=+2>", p->first,
        "</font></td></tr></table>",
        "</p>\n<td>\n<td>\n<td>\n<td>\n<td>\n",
        "<table bgcolor=#fff5ee frame=box cellspacing=1 cellpadding=2>\n",
        "<tr bgcolor=#eee5de><td>"
        "<form action=\"/statusz/reset\" method = \"post\">"
        "<input type=\"submit\" value = \"Reset Statistics\"></form></td>"
        "<th align=right>TenSec</th><th align=right>Minute</th>"
        "<th align=right>Hour</th><th align=right>Total</th></tr>");
    writer->Write(begin, message_handler);
    // Write each statistic as a row in the table.
    for (int i = 0, n = p->second.size(); i < n; ++i) {
      timedvar = FindTimedVariable(p->second[i]);
      const GoogleString content = StringPrintf("<tr><td> %s </td>"
          "<td align=right> %s </td><td align=right> %s </td>"
          "<td align=right> %s </td><td align=right> %s </td></tr>",
      p->second[i].c_str(),
      Integer64ToString(timedvar->Get(TimedVariable::TENSEC)).c_str(),
      Integer64ToString(timedvar->Get(TimedVariable::MINUTE)).c_str(),
      Integer64ToString(timedvar->Get(TimedVariable::HOUR)).c_str(),
      Integer64ToString(timedvar->Get(TimedVariable::START)).c_str());
      writer->Write(content, message_handler);
    }
    // Write table ending part.
    writer->Write(end, message_handler);
  }
}

int64 Statistics::LookupValue(StringPiece stat_name) {
  Variable* var = FindVariable(stat_name);
  if (var != NULL) {
    return var->Get();
  }
  UpDownCounter* counter = FindUpDownCounter(stat_name);
  if (counter != NULL) {
    return counter->Get();
  }
  TimedVariable* tvar = FindTimedVariable(stat_name);
  if (tvar != NULL) {
    return tvar->Get(TimedVariable::START);
  }
  LOG(FATAL) << "Could not find stat: " << stat_name;
  return 0;
}

}  // namespace net_instaweb
