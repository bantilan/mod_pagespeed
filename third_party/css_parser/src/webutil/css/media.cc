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

#include "third_party/css_parser/src/webutil/css/media.h"

#include "third_party/css_parser/src/util/gtl/stl_util.h"

namespace Css {

MediaExpression::~MediaExpression() {}
MediaExpressions::~MediaExpressions() { STLDeleteElements(this); }
MediaQuery::~MediaQuery() {}
MediaQueries::~MediaQueries() { STLDeleteElements(this); }

void MediaQueries::Clear() {
  STLDeleteElements(this);
  clear();
}

MediaQueries* MediaQueries::DeepCopy() const {
  MediaQueries* copy = new MediaQueries;
  for (int i = 0, n = this->size(); i < n; ++i) {
    copy->push_back(this->at(i)->DeepCopy());
  }
  return copy;
}

int64_t MediaQueries::ApproximateBytes() const {
  int64_t bytes = sizeof(MediaQueries);
  for (const MediaQuery* query : *this) {
    bytes += sizeof(MediaQuery) + query->media_type().utf8_length();
    for (int i = 0, n = query->expressions().size(); i < n; ++i) {
      const MediaExpression& expression = query->expression(i);
      bytes += sizeof(MediaExpression) + expression.name().utf8_length() +
               (expression.has_value() ? expression.value().utf8_length() : 0);
    }
  }
  return bytes;
}

MediaQuery* MediaQuery::DeepCopy() const {
  MediaQuery* copy = new MediaQuery;
  copy->set_qualifier(this->qualifier());
  copy->set_media_type(this->media_type());
  for (int i = 0, n = this->expressions().size(); i < n; ++i) {
    copy->add_expression(this->expression(i).DeepCopy());
  }
  return copy;
}

MediaExpression* MediaExpression::DeepCopy() const {
  if (this->has_value()) {
    return new MediaExpression(this->name(), this->value());
  } else {
    return new MediaExpression(this->name());
  }
}

}  // namespace Css
