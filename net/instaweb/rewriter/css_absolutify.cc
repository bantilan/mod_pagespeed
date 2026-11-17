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

#include "net/instaweb/rewriter/public/css_absolutify.h"

#include <cstddef>
#include <memory>
#include <vector>

#include "net/instaweb/rewriter/public/rewrite_driver.h"
#include "pagespeed/kernel/base/string.h"
#include "pagespeed/kernel/base/string_util.h"
#include "pagespeed/kernel/base/string_writer.h"
#include "pagespeed/kernel/http/google_url.h"
#include "third_party/css_parser/src/util/utf8/public/unicodetext.h"
#include "third_party/css_parser/src/webutil/css/parser.h"
#include "third_party/css_parser/src/webutil/css/property.h"
#include "third_party/css_parser/src/webutil/css/selector.h"
#include "third_party/css_parser/src/webutil/css/value.h"

namespace net_instaweb {

namespace {

// Returns a copy of the FUNCTION value 'function' in which every url() among
// its parameters, at any depth, has gone through transformer, or NULL when
// no parameter is a url() or a function holding one. Sets *saw_url when a
// url() was found.
Css::Value* TransformFunctionUrls(const Css::Value& function,
                                  CssTagScanner::Transformer* transformer,
                                  bool* saw_url) {
  const Css::FunctionParameters* params =
      function.GetParametersWithSeparators();
  if (params == NULL) {
    return NULL;
  }
  bool changed = false;
  std::unique_ptr<Css::FunctionParameters> copy(
      new Css::FunctionParameters());
  for (int i = 0, n = params->size(); i < n; ++i) {
    const Css::Value* param = params->value(i);
    std::unique_ptr<Css::Value> replacement;
    if (param->GetLexicalUnitType() == Css::Value::URI) {
      *saw_url = true;
      GoogleString url = UnicodeTextToUTF8(param->GetStringValue());
      if (transformer->Transform(&url) ==
          CssTagScanner::Transformer::kSuccess) {
        replacement.reset(new Css::Value(Css::Value::URI,
                                         UTF8ToUnicodeText(url)));
      }
    } else if (param->GetLexicalUnitType() == Css::Value::FUNCTION) {
      replacement.reset(TransformFunctionUrls(*param, transformer, saw_url));
    }
    if (replacement.get() != NULL) {
      changed = true;
    } else {
      replacement.reset(new Css::Value(*param));
    }
    copy->AddSepValue(params->separator(i), replacement.release());
  }
  if (!changed) {
    return NULL;
  }
  return new Css::Value(function.GetFunctionName(), copy.release());
}

}  // namespace

// TODO(sligocki): Move this into AbsolutifyUrls(). We don't ever want
// to use them separately. If we want to absolutify URLs, we want to
// absolutify them all.
bool CssAbsolutify::AbsolutifyImports(Css::Stylesheet* stylesheet,
                                      const GoogleUrl& base) {
  bool urls_modified = false;
  const Css::Imports& imports = stylesheet->imports();
  Css::Imports::const_iterator iter;
  for (iter = imports.begin(); iter != imports.end(); ++iter) {
    Css::Import* import = *iter;
    StringPiece url(import->link().utf8_data(), import->link().utf8_length());
    GoogleUrl gurl(base, url);
    if (gurl.IsWebValid() && gurl.Spec() != url) {
      url = gurl.Spec();
      import->set_link(UTF8ToUnicodeText(url.data(), url.length()));
      urls_modified = true;
    }
  }
  return urls_modified;
}

bool CssAbsolutify::AbsolutifyUrls(Css::Stylesheet* stylesheet,
                                   const GoogleUrl& base,
                                   bool handle_parseable_ruleset_sections,
                                   bool handle_unparseable_sections,
                                   RewriteDriver* driver,
                                   MessageHandler* handler) {
  RewriteDomainTransformer transformer(&base, &base, driver->server_context(),
                                       driver->options(),
                                       driver->message_handler());
  transformer.set_trim_urls(false);
  bool urls_modified = false;

  Css::FontFaces& font_faces = stylesheet->mutable_font_faces();
  for (Css::FontFaces::iterator font_face_iter = font_faces.begin();
       font_face_iter != font_faces.end(); ++font_face_iter) {
    Css::FontFace* font_face = *font_face_iter;
    if (AbsolutifyDeclarations(
            &font_face->mutable_declarations(), &transformer,
            true, /* Must handle parseable sections in @font-face. */
            handle_unparseable_sections, handler)) {
      urls_modified = true;
    }
  }

  // Absolutify URLs in unparseable selectors and declarations.
  Css::Rulesets& rulesets = stylesheet->mutable_rulesets();
  for (Css::Rulesets::iterator ruleset_iter = rulesets.begin();
       ruleset_iter != rulesets.end(); ++ruleset_iter) {
    Css::Ruleset* ruleset = *ruleset_iter;
    // Check any unparseable sections for any URLs and absolutify as required.
    if (handle_unparseable_sections) {
      switch (ruleset->type()) {
        case Css::Ruleset::RULESET: {
          Css::Selectors& selectors(ruleset->mutable_selectors());
          if (selectors.is_dummy()) {
            // XXX(oschaaf): css
            StringPiece tmp = selectors.bytes_in_original_buffer();
            StringPiece original_bytes(tmp.data(), tmp.size());
            GoogleString rewritten_bytes;
            StringWriter writer(&rewritten_bytes);
            if (CssTagScanner::TransformUrls(original_bytes, &writer,
                                             &transformer, handler)) {
              selectors.set_bytes_in_original_buffer(rewritten_bytes);
              urls_modified = true;
            }
          }
          break;
        }
        case Css::Ruleset::UNPARSED_REGION: {
          Css::UnparsedRegion* unparsed = ruleset->mutable_unparsed_region();
          // XXX(oschaaf): css
          StringPiece tmp = unparsed->bytes_in_original_buffer();
          StringPiece original_bytes(tmp.data(), tmp.size());
          GoogleString rewritten_bytes;
          StringWriter writer(&rewritten_bytes);
          if (CssTagScanner::TransformUrls(original_bytes, &writer,
                                           &transformer, handler)) {
            unparsed->set_bytes_in_original_buffer(rewritten_bytes);
            urls_modified = true;
          }
          break;
        }
      }
    }
    if (ruleset->type() == Css::Ruleset::RULESET) {
      if (AbsolutifyDeclarations(&ruleset->mutable_declarations(), &transformer,
                                 handle_parseable_ruleset_sections,
                                 handle_unparseable_sections, handler)) {
        urls_modified = true;
      }
    }
  }

  return urls_modified;
}

bool CssAbsolutify::AbsolutifyDeclarations(
    Css::Declarations* decls, CssTagScanner::Transformer* transformer,
    bool handle_parseable_sections, bool handle_unparseable_sections,
    MessageHandler* handler) {
  bool urls_modified = false;
  for (Css::Declarations::iterator decl_iter = decls->begin();
       decl_iter != decls->end(); ++decl_iter) {
    Css::Declaration* decl = *decl_iter;
    if (decl->prop() == Css::Property::UNPARSEABLE) {
      if (handle_unparseable_sections) {
        // XXX(oschaaf): css
        StringPiece tmp = decl->bytes_in_original_buffer();
        StringPiece original_bytes(tmp.data(), tmp.size());
        GoogleString rewritten_bytes;
        StringWriter writer(&rewritten_bytes);
        if (CssTagScanner::TransformUrls(original_bytes, &writer, transformer,
                                         handler)) {
          urls_modified = true;
          decl->set_bytes_in_original_buffer(rewritten_bytes);
        }
      }
    } else if (handle_parseable_sections) {
      Css::Values* values = decl->mutable_values();
      for (size_t value_index = 0; value_index < values->size();
           ++value_index) {
        Css::Value* value = values->at(value_index);
        if (value->GetLexicalUnitType() == Css::Value::URI) {
          urls_modified = true;
          GoogleString url = UnicodeTextToUTF8(value->GetStringValue());
          if (transformer->Transform(&url) ==
              CssTagScanner::Transformer::kSuccess) {
            delete (*values)[value_index];
            (*values)[value_index] =
                new Css::Value(Css::Value::URI, UTF8ToUnicodeText(url));
          }
        } else if (value->GetLexicalUnitType() == Css::Value::FUNCTION) {
          // A url() passed to a function, such as image-set(), is resolved
          // against the same base as one that stands on its own.
          Css::Value* replacement =
              TransformFunctionUrls(*value, transformer, &urls_modified);
          if (replacement != NULL) {
            delete (*values)[value_index];
            (*values)[value_index] = replacement;
          }
        }
      }
    }
  }
  return urls_modified;
}

}  // namespace net_instaweb
