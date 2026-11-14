(function(){var COMPILED = !0, goog = goog || {};
goog.global = this;
goog.isDef = function(a) {
  return void 0 !== a;
};
goog.exportPath_ = function(a, b, c) {
  a = a.split(".");
  c = c || goog.global;
  a[0] in c || !c.execScript || c.execScript("var " + a[0]);
  for (var d; a.length && (d = a.shift());) {
    !a.length && goog.isDef(b) ? c[d] = b : c = c[d] ? c[d] : c[d] = {};
  }
};
goog.define = function(a, b) {
  COMPILED || (goog.global.CLOSURE_UNCOMPILED_DEFINES && Object.prototype.hasOwnProperty.call(goog.global.CLOSURE_UNCOMPILED_DEFINES, a) ? b = goog.global.CLOSURE_UNCOMPILED_DEFINES[a] : goog.global.CLOSURE_DEFINES && Object.prototype.hasOwnProperty.call(goog.global.CLOSURE_DEFINES, a) && (b = goog.global.CLOSURE_DEFINES[a]));
  goog.exportPath_(a, b);
};
goog.DEBUG = !0;
goog.LOCALE = "en";
goog.TRUSTED_SITE = !0;
goog.STRICT_MODE_COMPATIBLE = !1;
goog.DISALLOW_TEST_ONLY_CODE = COMPILED && !goog.DEBUG;
goog.ENABLE_CHROME_APP_SAFE_SCRIPT_LOADING = !1;
goog.provide = function(a) {
  if (goog.isInModuleLoader_()) {
    throw Error("goog.provide can not be used within a goog.module.");
  }
  if (!COMPILED && goog.isProvided_(a)) {
    throw Error('Namespace "' + a + '" already declared.');
  }
  goog.constructNamespace_(a);
};
goog.constructNamespace_ = function(a, b) {
  if (!COMPILED) {
    delete goog.implicitNamespaces_[a];
    for (var c = a; (c = c.substring(0, c.lastIndexOf("."))) && !goog.getObjectByName(c);) {
      goog.implicitNamespaces_[c] = !0;
    }
  }
  goog.exportPath_(a, b);
};
goog.VALID_MODULE_RE_ = /^[a-zA-Z_$][a-zA-Z0-9._$]*$/;
goog.module = function(a) {
  if (!goog.isString(a) || !a || -1 == a.search(goog.VALID_MODULE_RE_)) {
    throw Error("Invalid module identifier");
  }
  if (!goog.isInModuleLoader_()) {
    throw Error("Module " + a + " has been loaded incorrectly. Note, modules cannot be loaded as normal scripts. They require some kind of pre-processing step. You're likely trying to load a module via a script tag or as a part of a concatenated bundle without rewriting the module. For more info see: https://github.com/google/closure-library/wiki/goog.module:-an-ES6-module-like-alternative-to-goog.provide.");
  }
  if (goog.moduleLoaderState_.moduleName) {
    throw Error("goog.module may only be called once per module.");
  }
  goog.moduleLoaderState_.moduleName = a;
  if (!COMPILED) {
    if (goog.isProvided_(a)) {
      throw Error('Namespace "' + a + '" already declared.');
    }
    delete goog.implicitNamespaces_[a];
  }
};
goog.module.get = function(a) {
  return goog.module.getInternal_(a);
};
goog.module.getInternal_ = function(a) {
  if (!COMPILED) {
    return goog.isProvided_(a) ? a in goog.loadedModules_ ? goog.loadedModules_[a] : goog.getObjectByName(a) : null;
  }
};
goog.moduleLoaderState_ = null;
goog.isInModuleLoader_ = function() {
  return null != goog.moduleLoaderState_;
};
goog.module.declareLegacyNamespace = function() {
  if (!COMPILED && !goog.isInModuleLoader_()) {
    throw Error("goog.module.declareLegacyNamespace must be called from within a goog.module");
  }
  if (!COMPILED && !goog.moduleLoaderState_.moduleName) {
    throw Error("goog.module must be called prior to goog.module.declareLegacyNamespace.");
  }
  goog.moduleLoaderState_.declareLegacyNamespace = !0;
};
goog.setTestOnly = function(a) {
  if (goog.DISALLOW_TEST_ONLY_CODE) {
    throw a = a || "", Error("Importing test-only code into non-debug environment" + (a ? ": " + a : "."));
  }
};
goog.forwardDeclare = function(a) {
};
COMPILED || (goog.isProvided_ = function(a) {
  return a in goog.loadedModules_ || !goog.implicitNamespaces_[a] && goog.isDefAndNotNull(goog.getObjectByName(a));
}, goog.implicitNamespaces_ = {"goog.module":!0});
goog.getObjectByName = function(a, b) {
  a = a.split(".");
  b = b || goog.global;
  for (var c; c = a.shift();) {
    if (goog.isDefAndNotNull(b[c])) {
      b = b[c];
    } else {
      return null;
    }
  }
  return b;
};
goog.globalize = function(a, b) {
  b = b || goog.global;
  for (var c in a) {
    b[c] = a[c];
  }
};
goog.addDependency = function(a, b, c, d) {
  if (goog.DEPENDENCIES_ENABLED) {
    var f;
    a = a.replace(/\\/g, "/");
    var e = goog.dependencies_;
    d && "boolean" !== typeof d || (d = d ? {module:"goog"} : {});
    for (var g = 0; f = b[g]; g++) {
      e.nameToPath[f] = a, e.loadFlags[a] = d;
    }
    for (d = 0; b = c[d]; d++) {
      a in e.requires || (e.requires[a] = {}), e.requires[a][b] = !0;
    }
  }
};
goog.ENABLE_DEBUG_LOADER = !0;
goog.logToConsole_ = function(a) {
  goog.global.console && goog.global.console.error(a);
};
goog.require = function(a) {
  if (!COMPILED) {
    goog.ENABLE_DEBUG_LOADER && goog.IS_OLD_IE_ && goog.maybeProcessDeferredDep_(a);
    if (goog.isProvided_(a)) {
      if (goog.isInModuleLoader_()) {
        return goog.module.getInternal_(a);
      }
    } else {
      if (goog.ENABLE_DEBUG_LOADER) {
        var b = goog.getPathFromDeps_(a);
        if (b) {
          goog.writeScripts_(b);
        } else {
          throw a = "goog.require could not find: " + a, goog.logToConsole_(a), Error(a);
        }
      }
    }
    return null;
  }
};
goog.basePath = "";
goog.nullFunction = function() {
};
goog.abstractMethod = function() {
  throw Error("unimplemented abstract method");
};
goog.addSingletonGetter = function(a) {
  a.getInstance = function() {
    if (a.instance_) {
      return a.instance_;
    }
    goog.DEBUG && (goog.instantiatedSingletons_[goog.instantiatedSingletons_.length] = a);
    return a.instance_ = new a;
  };
};
goog.instantiatedSingletons_ = [];
goog.LOAD_MODULE_USING_EVAL = !0;
goog.SEAL_MODULE_EXPORTS = goog.DEBUG;
goog.loadedModules_ = {};
goog.DEPENDENCIES_ENABLED = !COMPILED && goog.ENABLE_DEBUG_LOADER;
goog.TRANSPILE = "detect";
goog.TRANSPILER = "transpile.js";
goog.DEPENDENCIES_ENABLED && (goog.dependencies_ = {loadFlags:{}, nameToPath:{}, requires:{}, visited:{}, written:{}, deferred:{}}, goog.inHtmlDocument_ = function() {
  var a = goog.global.document;
  return null != a && "write" in a;
}, goog.findBasePath_ = function() {
  if (goog.isDef(goog.global.CLOSURE_BASE_PATH)) {
    goog.basePath = goog.global.CLOSURE_BASE_PATH;
  } else {
    if (goog.inHtmlDocument_()) {
      for (var a = goog.global.document.getElementsByTagName("SCRIPT"), b = a.length - 1; 0 <= b; --b) {
        var c = a[b].src, d = c.lastIndexOf("?");
        d = -1 == d ? c.length : d;
        if ("base.js" == c.substr(d - 7, 7)) {
          goog.basePath = c.substr(0, d - 7);
          break;
        }
      }
    }
  }
}, goog.importScript_ = function(a, b) {
  (goog.global.CLOSURE_IMPORT_SCRIPT || goog.writeScriptTag_)(a, b) && (goog.dependencies_.written[a] = !0);
}, goog.IS_OLD_IE_ = !(goog.global.atob || !goog.global.document || !goog.global.document.all), goog.importProcessedScript_ = function(a, b, c) {
  goog.importScript_("", 'goog.retrieveAndExec_("' + a + '", ' + b + ", " + c + ");");
}, goog.queuedModules_ = [], goog.wrapModule_ = function(a, b) {
  return goog.LOAD_MODULE_USING_EVAL && goog.isDef(goog.global.JSON) ? "goog.loadModule(" + goog.global.JSON.stringify(b + "\n//# sourceURL=" + a + "\n") + ");" : 'goog.loadModule(function(exports) {"use strict";' + b + "\n;return exports});\n//# sourceURL=" + a + "\n";
}, goog.loadQueuedModules_ = function() {
  var a = goog.queuedModules_.length;
  if (0 < a) {
    var b = goog.queuedModules_;
    goog.queuedModules_ = [];
    for (var c = 0; c < a; c++) {
      goog.maybeProcessDeferredPath_(b[c]);
    }
  }
}, goog.maybeProcessDeferredDep_ = function(a) {
  goog.isDeferredModule_(a) && goog.allDepsAreAvailable_(a) && (a = goog.getPathFromDeps_(a), goog.maybeProcessDeferredPath_(goog.basePath + a));
}, goog.isDeferredModule_ = function(a) {
  var b = (a = goog.getPathFromDeps_(a)) && goog.dependencies_.loadFlags[a] || {}, c = b.lang || "es3";
  return a && ("goog" == b.module || goog.needsTranspile_(c)) ? goog.basePath + a in goog.dependencies_.deferred : !1;
}, goog.allDepsAreAvailable_ = function(a) {
  if ((a = goog.getPathFromDeps_(a)) && a in goog.dependencies_.requires) {
    for (var b in goog.dependencies_.requires[a]) {
      if (!goog.isProvided_(b) && !goog.isDeferredModule_(b)) {
        return !1;
      }
    }
  }
  return !0;
}, goog.maybeProcessDeferredPath_ = function(a) {
  if (a in goog.dependencies_.deferred) {
    var b = goog.dependencies_.deferred[a];
    delete goog.dependencies_.deferred[a];
    goog.globalEval(b);
  }
}, goog.loadModuleFromUrl = function(a) {
  goog.retrieveAndExec_(a, !0, !1);
}, goog.writeScriptSrcNode_ = function(a) {
  goog.global.document.write('<script type="text/javascript" src="' + a + '">\x3c/script>');
}, goog.appendScriptSrcNode_ = function(a) {
  var b = goog.global.document, c = b.createElement("script");
  c.type = "text/javascript";
  c.src = a;
  c.defer = !1;
  c.async = !1;
  b.head.appendChild(c);
}, goog.writeScriptTag_ = function(a, b) {
  if (goog.inHtmlDocument_()) {
    var c = goog.global.document;
    if (!goog.ENABLE_CHROME_APP_SAFE_SCRIPT_LOADING && "complete" == c.readyState) {
      if (/\bdeps.js$/.test(a)) {
        return !1;
      }
      throw Error('Cannot write "' + a + '" after document load');
    }
    void 0 === b ? goog.IS_OLD_IE_ ? (b = " onreadystatechange='goog.onScriptLoad_(this, " + ++goog.lastNonModuleScriptIndex_ + ")' ", c.write('<script type="text/javascript" src="' + a + '"' + b + ">\x3c/script>")) : goog.ENABLE_CHROME_APP_SAFE_SCRIPT_LOADING ? goog.appendScriptSrcNode_(a) : goog.writeScriptSrcNode_(a) : c.write('<script type="text/javascript">' + b + "\x3c/script>");
    return !0;
  }
  return !1;
}, goog.needsTranspile_ = function(a) {
  if ("always" == goog.TRANSPILE) {
    return !0;
  }
  if ("never" == goog.TRANSPILE) {
    return !1;
  }
  goog.requiresTranspilation_ || (goog.requiresTranspilation_ = goog.createRequiresTranspilation_());
  if (a in goog.requiresTranspilation_) {
    return goog.requiresTranspilation_[a];
  }
  throw Error("Unknown language mode: " + a);
}, goog.createRequiresTranspilation_ = function() {
  function a(a, b) {
    d ? c[a] = !0 : b() ? c[a] = !1 : d = c[a] = !0;
  }
  function b(a) {
    try {
      return !!eval(a);
    } catch (e) {
      return !1;
    }
  }
  var c = {es3:!1}, d = !1;
  a("es5", function() {
    return b("[1,].length==1");
  });
  a("es6", function() {
    return b('(()=>{"use strict";class X{constructor(){if(new.target!=String)throw 1;this.x=42}}let q=Reflect.construct(X,[],String);if(q.x!=42||!(q instanceof String))throw 1;for(const a of[2,3]){if(a==2)continue;function f(z={a}){let a=0;return z.a}{function f(){return 0;}}return f()==3}})()');
  });
  a("es6-impl", function() {
    return !0;
  });
  a("es7", function() {
    return b("2 ** 2 == 4");
  });
  a("es8", function() {
    return b("async () => 1, true");
  });
  return c;
}, goog.requiresTranspilation_ = null, goog.lastNonModuleScriptIndex_ = 0, goog.onScriptLoad_ = function(a, b) {
  "complete" == a.readyState && goog.lastNonModuleScriptIndex_ == b && goog.loadQueuedModules_();
  return !0;
}, goog.writeScripts_ = function(a) {
  function b(a) {
    if (!(a in f.written || a in f.visited)) {
      f.visited[a] = !0;
      if (a in f.requires) {
        for (var e in f.requires[a]) {
          if (!goog.isProvided_(e)) {
            if (e in f.nameToPath) {
              b(f.nameToPath[e]);
            } else {
              throw Error("Undefined nameToPath for " + e);
            }
          }
        }
      }
      a in d || (d[a] = !0, c.push(a));
    }
  }
  var c = [], d = {}, f = goog.dependencies_;
  b(a);
  for (a = 0; a < c.length; a++) {
    var e = c[a];
    goog.dependencies_.written[e] = !0;
  }
  var g = goog.moduleLoaderState_;
  goog.moduleLoaderState_ = null;
  for (a = 0; a < c.length; a++) {
    if (e = c[a]) {
      var m = f.loadFlags[e] || {}, h = goog.needsTranspile_(m.lang || "es3");
      "goog" == m.module || h ? goog.importProcessedScript_(goog.basePath + e, "goog" == m.module, h) : goog.importScript_(goog.basePath + e);
    } else {
      throw goog.moduleLoaderState_ = g, Error("Undefined script input");
    }
  }
  goog.moduleLoaderState_ = g;
}, goog.getPathFromDeps_ = function(a) {
  return a in goog.dependencies_.nameToPath ? goog.dependencies_.nameToPath[a] : null;
}, goog.findBasePath_(), goog.global.CLOSURE_NO_DEPS || goog.importScript_(goog.basePath + "deps.js"));
goog.loadModule = function(a) {
  var b = goog.moduleLoaderState_;
  try {
    goog.moduleLoaderState_ = {moduleName:void 0, declareLegacyNamespace:!1};
    if (goog.isFunction(a)) {
      var c = a.call(void 0, {});
    } else {
      if (goog.isString(a)) {
        c = goog.loadModuleFromSource_.call(void 0, a);
      } else {
        throw Error("Invalid module definition");
      }
    }
    var d = goog.moduleLoaderState_.moduleName;
    if (!goog.isString(d) || !d) {
      throw Error('Invalid module name "' + d + '"');
    }
    goog.moduleLoaderState_.declareLegacyNamespace ? goog.constructNamespace_(d, c) : goog.SEAL_MODULE_EXPORTS && Object.seal && goog.isObject(c) && Object.seal(c);
    goog.loadedModules_[d] = c;
  } finally {
    goog.moduleLoaderState_ = b;
  }
};
goog.loadModuleFromSource_ = function(a) {
  eval(a);
  return {};
};
goog.normalizePath_ = function(a) {
  a = a.split("/");
  for (var b = 0; b < a.length;) {
    "." == a[b] ? a.splice(b, 1) : b && ".." == a[b] && a[b - 1] && ".." != a[b - 1] ? a.splice(--b, 2) : b++;
  }
  return a.join("/");
};
goog.loadFileSync_ = function(a) {
  if (goog.global.CLOSURE_LOAD_FILE_SYNC) {
    return goog.global.CLOSURE_LOAD_FILE_SYNC(a);
  }
  try {
    var b = new goog.global.XMLHttpRequest;
    b.open("get", a, !1);
    b.send();
    return 0 == b.status || 200 == b.status ? b.responseText : null;
  } catch (c) {
    return null;
  }
};
goog.retrieveAndExec_ = function(a, b, c) {
  if (!COMPILED) {
    var d = a;
    a = goog.normalizePath_(a);
    var f = goog.global.CLOSURE_IMPORT_SCRIPT || goog.writeScriptTag_, e = goog.loadFileSync_(a);
    if (null == e) {
      throw Error('Load of "' + a + '" failed');
    }
    c && (e = goog.transpile_.call(goog.global, e, a));
    e = b ? goog.wrapModule_(a, e) : e + ("\n//# sourceURL=" + a);
    goog.IS_OLD_IE_ ? (goog.dependencies_.deferred[d] = e, goog.queuedModules_.push(d)) : f(a, e);
  }
};
goog.transpile_ = function(a, b) {
  var c = goog.global.$jscomp;
  c || (goog.global.$jscomp = c = {});
  var d = c.transpile;
  if (!d) {
    var f = goog.basePath + goog.TRANSPILER, e = goog.loadFileSync_(f);
    if (e) {
      eval(e + "\n//# sourceURL=" + f);
      if (goog.global.$gwtExport && goog.global.$gwtExport.$jscomp && !goog.global.$gwtExport.$jscomp.transpile) {
        throw Error('The transpiler did not properly export the "transpile" method. $gwtExport: ' + JSON.stringify(goog.global.$gwtExport));
      }
      goog.global.$jscomp.transpile = goog.global.$gwtExport.$jscomp.transpile;
      c = goog.global.$jscomp;
      d = c.transpile;
    }
  }
  d || (d = c.transpile = function(a, b) {
    goog.logToConsole_(b + " requires transpilation but no transpiler was found.");
    return a;
  });
  return d(a, b);
};
goog.typeOf = function(a) {
  var b = typeof a;
  if ("object" == b) {
    if (a) {
      if (a instanceof Array) {
        return "array";
      }
      if (a instanceof Object) {
        return b;
      }
      var c = Object.prototype.toString.call(a);
      if ("[object Window]" == c) {
        return "object";
      }
      if ("[object Array]" == c || "number" == typeof a.length && "undefined" != typeof a.splice && "undefined" != typeof a.propertyIsEnumerable && !a.propertyIsEnumerable("splice")) {
        return "array";
      }
      if ("[object Function]" == c || "undefined" != typeof a.call && "undefined" != typeof a.propertyIsEnumerable && !a.propertyIsEnumerable("call")) {
        return "function";
      }
    } else {
      return "null";
    }
  } else {
    if ("function" == b && "undefined" == typeof a.call) {
      return "object";
    }
  }
  return b;
};
goog.isNull = function(a) {
  return null === a;
};
goog.isDefAndNotNull = function(a) {
  return null != a;
};
goog.isArray = function(a) {
  return "array" == goog.typeOf(a);
};
goog.isArrayLike = function(a) {
  var b = goog.typeOf(a);
  return "array" == b || "object" == b && "number" == typeof a.length;
};
goog.isDateLike = function(a) {
  return goog.isObject(a) && "function" == typeof a.getFullYear;
};
goog.isString = function(a) {
  return "string" == typeof a;
};
goog.isBoolean = function(a) {
  return "boolean" == typeof a;
};
goog.isNumber = function(a) {
  return "number" == typeof a;
};
goog.isFunction = function(a) {
  return "function" == goog.typeOf(a);
};
goog.isObject = function(a) {
  var b = typeof a;
  return "object" == b && null != a || "function" == b;
};
goog.getUid = function(a) {
  return a[goog.UID_PROPERTY_] || (a[goog.UID_PROPERTY_] = ++goog.uidCounter_);
};
goog.hasUid = function(a) {
  return !!a[goog.UID_PROPERTY_];
};
goog.removeUid = function(a) {
  null !== a && "removeAttribute" in a && a.removeAttribute(goog.UID_PROPERTY_);
  try {
    delete a[goog.UID_PROPERTY_];
  } catch (b) {
  }
};
goog.UID_PROPERTY_ = "closure_uid_" + (1e9 * Math.random() >>> 0);
goog.uidCounter_ = 0;
goog.getHashCode = goog.getUid;
goog.removeHashCode = goog.removeUid;
goog.cloneObject = function(a) {
  var b = goog.typeOf(a);
  if ("object" == b || "array" == b) {
    if (a.clone) {
      return a.clone();
    }
    b = "array" == b ? [] : {};
    for (var c in a) {
      b[c] = goog.cloneObject(a[c]);
    }
    return b;
  }
  return a;
};
goog.bindNative_ = function(a, b, c) {
  return a.call.apply(a.bind, arguments);
};
goog.bindJs_ = function(a, b, c) {
  if (!a) {
    throw Error();
  }
  if (2 < arguments.length) {
    var d = Array.prototype.slice.call(arguments, 2);
    return function() {
      var c = Array.prototype.slice.call(arguments);
      Array.prototype.unshift.apply(c, d);
      return a.apply(b, c);
    };
  }
  return function() {
    return a.apply(b, arguments);
  };
};
goog.bind = function(a, b, c) {
  Function.prototype.bind && -1 != Function.prototype.bind.toString().indexOf("native code") ? goog.bind = goog.bindNative_ : goog.bind = goog.bindJs_;
  return goog.bind.apply(null, arguments);
};
goog.partial = function(a, b) {
  var c = Array.prototype.slice.call(arguments, 1);
  return function() {
    var b = c.slice();
    b.push.apply(b, arguments);
    return a.apply(this, b);
  };
};
goog.mixin = function(a, b) {
  for (var c in b) {
    a[c] = b[c];
  }
};
goog.now = goog.TRUSTED_SITE && Date.now || function() {
  return +new Date;
};
goog.globalEval = function(a) {
  if (goog.global.execScript) {
    goog.global.execScript(a, "JavaScript");
  } else {
    if (goog.global.eval) {
      if (null == goog.evalWorksForGlobals_) {
        if (goog.global.eval("var _evalTest_ = 1;"), "undefined" != typeof goog.global._evalTest_) {
          try {
            delete goog.global._evalTest_;
          } catch (d) {
          }
          goog.evalWorksForGlobals_ = !0;
        } else {
          goog.evalWorksForGlobals_ = !1;
        }
      }
      if (goog.evalWorksForGlobals_) {
        goog.global.eval(a);
      } else {
        var b = goog.global.document, c = b.createElement("SCRIPT");
        c.type = "text/javascript";
        c.defer = !1;
        c.appendChild(b.createTextNode(a));
        b.body.appendChild(c);
        b.body.removeChild(c);
      }
    } else {
      throw Error("goog.globalEval not available");
    }
  }
};
goog.evalWorksForGlobals_ = null;
goog.getCssName = function(a, b) {
  if ("." == String(a).charAt(0)) {
    throw Error('className passed in goog.getCssName must not start with ".". You passed: ' + a);
  }
  var c = function(a) {
    return goog.cssNameMapping_[a] || a;
  }, d = function(a) {
    a = a.split("-");
    for (var b = [], d = 0; d < a.length; d++) {
      b.push(c(a[d]));
    }
    return b.join("-");
  };
  d = goog.cssNameMapping_ ? "BY_WHOLE" == goog.cssNameMappingStyle_ ? c : d : function(a) {
    return a;
  };
  a = b ? a + "-" + d(b) : d(a);
  return goog.global.CLOSURE_CSS_NAME_MAP_FN ? goog.global.CLOSURE_CSS_NAME_MAP_FN(a) : a;
};
goog.setCssNameMapping = function(a, b) {
  goog.cssNameMapping_ = a;
  goog.cssNameMappingStyle_ = b;
};
!COMPILED && goog.global.CLOSURE_CSS_NAME_MAPPING && (goog.cssNameMapping_ = goog.global.CLOSURE_CSS_NAME_MAPPING);
goog.getMsg = function(a, b) {
  b && (a = a.replace(/\{\$([^}]+)}/g, function(a, d) {
    return null != b && d in b ? b[d] : a;
  }));
  return a;
};
goog.getMsgWithFallback = function(a, b) {
  return a;
};
goog.exportSymbol = function(a, b, c) {
  goog.exportPath_(a, b, c);
};
goog.exportProperty = function(a, b, c) {
  a[b] = c;
};
goog.inherits = function(a, b) {
  function c() {
  }
  c.prototype = b.prototype;
  a.superClass_ = b.prototype;
  a.prototype = new c;
  a.prototype.constructor = a;
  a.base = function(a, c, e) {
    for (var d = Array(arguments.length - 2), f = 2; f < arguments.length; f++) {
      d[f - 2] = arguments[f];
    }
    return b.prototype[c].apply(a, d);
  };
};
goog.base = function(a, b, c) {
  var d = arguments.callee.caller;
  if (goog.STRICT_MODE_COMPATIBLE || goog.DEBUG && !d) {
    throw Error("arguments.caller not defined.  goog.base() cannot be used with strict mode code. See http://www.ecma-international.org/ecma-262/5.1/#sec-C");
  }
  if (d.superClass_) {
    for (var f = Array(arguments.length - 1), e = 1; e < arguments.length; e++) {
      f[e - 1] = arguments[e];
    }
    return d.superClass_.constructor.apply(a, f);
  }
  f = Array(arguments.length - 2);
  for (e = 2; e < arguments.length; e++) {
    f[e - 2] = arguments[e];
  }
  e = !1;
  for (var g = a.constructor; g; g = g.superClass_ && g.superClass_.constructor) {
    if (g.prototype[b] === d) {
      e = !0;
    } else {
      if (e) {
        return g.prototype[b].apply(a, f);
      }
    }
  }
  if (a[b] === d) {
    return a.constructor.prototype[b].apply(a, f);
  }
  throw Error("goog.base called from a method of one name to a method of a different name");
};
goog.scope = function(a) {
  if (goog.isInModuleLoader_()) {
    throw Error("goog.scope is not supported within a goog.module.");
  }
  a.call(goog.global);
};
COMPILED || (goog.global.COMPILED = COMPILED);
goog.defineClass = function(a, b) {
  var c = b.constructor, d = b.statics;
  c && c != Object.prototype.constructor || (c = function() {
    throw Error("cannot instantiate an interface (no constructor defined).");
  });
  c = goog.defineClass.createSealingConstructor_(c, a);
  a && goog.inherits(c, a);
  delete b.constructor;
  delete b.statics;
  goog.defineClass.applyProperties_(c.prototype, b);
  null != d && (d instanceof Function ? d(c) : goog.defineClass.applyProperties_(c, d));
  return c;
};
goog.defineClass.SEAL_CLASS_INSTANCES = goog.DEBUG;
goog.defineClass.createSealingConstructor_ = function(a, b) {
  if (!goog.defineClass.SEAL_CLASS_INSTANCES) {
    return a;
  }
  var c = !goog.defineClass.isUnsealable_(b), d = function() {
    var b = a.apply(this, arguments) || this;
    b[goog.UID_PROPERTY_] = b[goog.UID_PROPERTY_];
    this.constructor === d && c && Object.seal instanceof Function && Object.seal(b);
    return b;
  };
  return d;
};
goog.defineClass.isUnsealable_ = function(a) {
  return a && a.prototype && a.prototype[goog.UNSEALABLE_CONSTRUCTOR_PROPERTY_];
};
goog.defineClass.OBJECT_PROTOTYPE_FIELDS_ = "constructor hasOwnProperty isPrototypeOf propertyIsEnumerable toLocaleString toString valueOf".split(" ");
goog.defineClass.applyProperties_ = function(a, b) {
  for (var c in b) {
    Object.prototype.hasOwnProperty.call(b, c) && (a[c] = b[c]);
  }
  for (var d = 0; d < goog.defineClass.OBJECT_PROTOTYPE_FIELDS_.length; d++) {
    c = goog.defineClass.OBJECT_PROTOTYPE_FIELDS_[d], Object.prototype.hasOwnProperty.call(b, c) && (a[c] = b[c]);
  }
};
goog.tagUnsealableClass = function(a) {
  !COMPILED && goog.defineClass.SEAL_CLASS_INSTANCES && (a.prototype[goog.UNSEALABLE_CONSTRUCTOR_PROPERTY_] = !0);
};
goog.UNSEALABLE_CONSTRUCTOR_PROPERTY_ = "goog_defineClass_legacy_unsealable";
var pagespeed = {Configuration:function() {
}};
pagespeed.Configuration.Start = function() {
  var a = document.getElementById("pagespeed-config-source"), b = document.getElementById("pagespeed-config-root");
  if (a && b) {
    var c = pagespeed.Configuration.parse_(a.textContent);
    pagespeed.Configuration.render_(b, c);
    a.style.display = "none";
  }
};
pagespeed.Configuration.parse_ = function(a) {
  var b = {version:"Not reported", filters:[], options:[], invalidation:"", raw:a}, c = "";
  a = a.split("\n");
  for (var d = 0; d < a.length; d++) {
    var f = a[d], e = f.trim(), g = e.match(/^Version:\s*([^:]+)/);
    g && (b.version = g[1].trim());
    "Filters" == e || "Options" == e || "Domain Lawyer" == e ? c = e : "Filters" == c && e ? (f = e.match(/^([a-zA-Z0-9_-]{1,4})\s+(.+)$/)) && b.filters.push({code:f[1], name:f[2].trim(), group:pagespeed.Configuration.filterGroup_(f[2])}) : "Options" == c && e ? (f = f.match(/^\s*(.*?)\s+\(([^)]+)\)\s{2,}(.*)$/)) && b.options.push({name:f[1].trim(), alias:f[2].trim(), value:f[3].trim()}) : "Domain Lawyer" == c && 0 == e.indexOf("Invalidation Timestamp:") && (b.invalidation = e.substring(23).trim());
  }
  return b;
};
pagespeed.Configuration.filterGroup_ = function(a) {
  return /css|style|flatten/i.test(a) ? "CSS" : /javascript|analytics|script/i.test(a) ? "JavaScript" : /image|gif|jpeg|png|webp|avif|resize|recompress/i.test(a) ? "Images" : "Other";
};
pagespeed.Configuration.description_ = function(a) {
  return {AdminPath:"Path used to access the PageSpeed administration console.", AnalyticsID:"Analytics property identifier used by the analytics filter.", AvoidRenamingIntrospectiveJavascript:"Avoid renaming JavaScript that inspects its own source.", BeaconUrl:"Endpoint used by PageSpeed beacon requests.", CriticalImagesBeaconEnabled:"Collect image-priority data from browser beacons.", EnableCachePurge:"Allow authorized cache purge requests.", EnableRewriting:"Enable PageSpeed resource rewriting.", 
  FileCacheInodeLimit:"Maximum number of inodes used by the file cache.", FileCachePath:"Filesystem location used for the PageSpeed cache.", InPlaceSMaxAgeSec:"Shared-cache lifetime for in-place rewritten resources.", LogDir:"Directory for PageSpeed message and statistics logs.", RewriteLevel:"Default set of rewrite filters applied to resources.", Statistics:"Expose PageSpeed statistics to the admin interface.", StatisticsLogging:"Persist statistics samples for graphs and history.", SupportNoScriptEnabled:"Enable alternate output for clients without scripts."}[a] || 
  "Effective PageSpeed setting for " + a.replace(/([a-z])([A-Z])/g, "$1 $2") + ".";
};
pagespeed.Configuration.render_ = function(a, b) {
  for (; a.firstChild;) {
    a.removeChild(a.firstChild);
  }
  var c = document.createElement("section");
  c.className = "pagespeed-config-notice";
  c.innerHTML = '<span class="pagespeed-config-notice-icon">i</span><div><strong>Read-only configuration</strong><p>These settings reflect the active PageSpeed configuration. To make changes, update your server configuration and reload the service.</p></div>';
  a.appendChild(c);
  var d = {};
  for (c = 0; c < b.options.length; c++) {
    d[b.options[c].name] = b.options[c].value;
  }
  var f = document.createElement("div");
  f.className = "pagespeed-config-summary";
  d = [["Configuration version", b.version, "Current PageSpeed settings format"], ["Rewrite level", d.RewriteLevel || "Not specified", "Default active filter set"], ["Rewriting", pagespeed.Configuration.isEnabled_(d.EnableRewriting) ? "Enabled" : "Disabled", "Resource optimization status"], ["Statistics", pagespeed.Configuration.isEnabled_(d.Statistics) ? "Enabled" : "Disabled", "Admin metrics availability"]];
  for (c = 0; c < d.length; c++) {
    var e = document.createElement("article");
    e.className = "pagespeed-config-summary-card";
    var g = document.createElement("span");
    g.className = "pagespeed-config-summary-label";
    g.textContent = d[c][0];
    var m = document.createElement("strong");
    m.className = "pagespeed-config-summary-value";
    m.textContent = d[c][1];
    var h = document.createElement("span");
    h.className = "pagespeed-config-summary-note";
    h.textContent = d[c][2];
    e.appendChild(g);
    e.appendChild(m);
    e.appendChild(h);
    f.appendChild(e);
  }
  a.appendChild(f);
  f = document.createElement("div");
  f.className = "pagespeed-config-columns";
  d = document.createElement("section");
  d.className = "pagespeed-config-card";
  d.innerHTML = '<div class="pagespeed-config-section-heading"><div><h2>Active filters</h2><p>PageSpeed filters currently enabled.</p></div></div>';
  e = d.querySelector(".pagespeed-config-section-heading");
  var n = document.createElement("button");
  n.type = "button";
  n.className = "pagespeed-config-view-all";
  n.textContent = "View full configuration";
  n.setAttribute("aria-expanded", "false");
  m = ["CSS", "JavaScript", "Images", "Other"];
  for (c = 0; c < m.length; c++) {
    h = [];
    for (var l = 0; l < b.filters.length; l++) {
      b.filters[l].group == m[c] && h.push(b.filters[l]);
    }
    if (h.length) {
      var u = document.createElement("div");
      u.className = "pagespeed-config-filter-group pagespeed-config-filter-" + m[c].toLowerCase().replace(" ", "-");
      g = document.createElement("div");
      g.className = "pagespeed-config-filter-heading";
      l = document.createElement("strong");
      l.textContent = m[c] + " (" + h.length + ")";
      var p = document.createElement("span");
      p.className = "pagespeed-config-enabled";
      p.textContent = "Enabled";
      g.appendChild(l);
      g.appendChild(p);
      p = document.createElement("ul");
      p.className = "pagespeed-config-filter-list";
      var v = Math.min(5, h.length);
      for (l = 0; l < h.length; l++) {
        var w = document.createElement("li"), z = document.createElement("span");
        z.className = "pagespeed-config-filter-name";
        z.textContent = h[l].name;
        var x = document.createElement("code");
        x.className = "pagespeed-config-filter-code";
        x.textContent = h[l].code;
        x.title = "PageSpeed filter ID";
        w.appendChild(z);
        w.appendChild(x);
        w.hidden = l >= v;
        p.appendChild(w);
      }
      u.appendChild(g);
      u.appendChild(p);
      if (h.length > v) {
        var k = document.createElement("button");
        k.type = "button";
        k.className = "pagespeed-config-filter-more";
        k.textContent = "Show " + (h.length - v) + " more";
        k.setAttribute("aria-expanded", "false");
        k.addEventListener("click", function(a, b, c, d) {
          return function() {
            for (var e = "true" == b.getAttribute("aria-expanded"), f = 0; f < a.length; f++) {
              f >= d && (a[f].hidden = e);
            }
            b.setAttribute("aria-expanded", e ? "false" : "true");
            b.textContent = e ? "Show " + c + " more" : "Show less";
          };
        }(p.children, k, h.length - v, v));
        u.appendChild(k);
      }
      d.appendChild(u);
    }
  }
  b.filters.length || (c = document.createElement("p"), c.className = "pagespeed-config-empty", c.textContent = "No active filters were listed.", d.appendChild(c));
  var q = document.createElement("pre");
  q.className = "pagespeed-config-full";
  q.hidden = !0;
  q.textContent = b.raw;
  n.addEventListener("click", function() {
    var a = !q.hidden;
    q.hidden = a;
    n.textContent = a ? "View full configuration" : "Hide full configuration";
    n.setAttribute("aria-expanded", a ? "false" : "true");
  });
  e.appendChild(n);
  d.appendChild(q);
  f.appendChild(d);
  c = document.createElement("section");
  c.className = "pagespeed-config-card pagespeed-config-options-card";
  d = document.createElement("div");
  d.className = "pagespeed-config-section-heading";
  d.innerHTML = "<div><h2>Options</h2><p>Effective values for key PageSpeed options.</p></div>";
  e = document.createElement("div");
  e.className = "pagespeed-config-options-toolbar";
  var r = document.createElement("input");
  r.type = "search";
  r.placeholder = "Search options...";
  r.setAttribute("aria-label", "Search configuration options");
  e.appendChild(r);
  d.appendChild(e);
  c.appendChild(d);
  d = document.createElement("div");
  d.className = "pagespeed-admin-table-wrap";
  e = document.createElement("table");
  e.className = "pagespeed-admin-data-table pagespeed-config-table";
  var B = e.createTHead().insertRow(0);
  ["Option", "Value", "Description", ""].forEach(function(a) {
    var b = document.createElement("th");
    b.textContent = a;
    B.appendChild(b);
  });
  var y = e.createTBody();
  k = document.createElement("button");
  k.type = "button";
  k.className = "pagespeed-config-more";
  k.setAttribute("aria-expanded", "false");
  c.appendChild(d);
  c.appendChild(k);
  d.appendChild(e);
  var t = !1, A = function() {
    for (; y.firstChild;) {
      y.removeChild(y.firstChild);
    }
    for (var a = r.value.toLowerCase(), c = [], d = 0; d < b.options.length; d++) {
      var e = b.options[d];
      0 <= (e.name + " " + e.alias + " " + e.value + " " + pagespeed.Configuration.description_(e.name)).toLowerCase().indexOf(a) && c.push(e);
    }
    a = a || t ? c.length : Math.min(9, c.length);
    for (d = 0; d < a; d++) {
      e = c[d];
      var f = y.insertRow(-1), g = f.insertCell(0);
      g.innerHTML = "<strong>" + pagespeed.Configuration.escape_(e.name) + "</strong><small>(" + pagespeed.Configuration.escape_(e.alias) + ")</small>";
      g.className = "pagespeed-config-option-name";
      g = f.insertCell(1);
      g.textContent = e.value;
      /^(true|false)$/i.test(e.value) && (g.innerHTML = '<span class="pagespeed-config-value-badge ' + (/^true$/i.test(e.value) ? "is-true" : "is-false") + '">' + pagespeed.Configuration.escape_(e.value) + "</span>");
      f.insertCell(2).textContent = pagespeed.Configuration.description_(e.name);
      f = f.insertCell(3);
      g = document.createElement("button");
      g.type = "button";
      g.className = "pagespeed-config-copy";
      g.textContent = "Copy";
      g.setAttribute("aria-label", "Copy " + e.name + " value");
      g.addEventListener("click", function(a, b) {
        return function() {
          pagespeed.Configuration.copy_(a, b);
        };
      }(e.value, g));
      f.appendChild(g);
    }
    c = c.length - a;
    k.style.display = 0 < c || t ? "" : "none";
    k.textContent = t ? "Show fewer options" : "Show " + c + " more option" + (1 == c ? "" : "s");
    k.setAttribute("aria-expanded", t ? "true" : "false");
  };
  r.addEventListener("input", A);
  k.addEventListener("click", function() {
    t = !t;
    A();
  });
  A();
  f.appendChild(c);
  a.appendChild(f);
  c = document.createElement("section");
  c.className = "pagespeed-config-card pagespeed-config-domain";
  c.innerHTML = '<div class="pagespeed-config-section-heading"><div><h2>Domain Lawyer</h2><p>Domain-specific configuration details.</p></div></div>';
  f = document.createElement("p");
  f.className = "pagespeed-config-timestamp";
  f.textContent = b.invalidation || "No invalidation timestamp reported.";
  c.appendChild(f);
  a.appendChild(c);
  a = document.createElement("button");
  a.type = "button";
  a.className = "pagespeed-config-export";
  a.textContent = "Export Configuration";
  a.addEventListener("click", function() {
    var a = new Blob([document.getElementById("pagespeed-config-source").textContent], {type:"text/plain;charset=utf-8"});
    a = URL.createObjectURL(a);
    var b = document.createElement("a");
    b.href = a;
    b.download = "pagespeed-configuration.txt";
    b.click();
    URL.revokeObjectURL(a);
  });
  (g = document.querySelector(".pagespeed-dashboard-heading")) && g.appendChild(a);
};
pagespeed.Configuration.isEnabled_ = function(a) {
  return !!a && !/^(false|0|off|no)$/i.test(a);
};
pagespeed.Configuration.escape_ = function(a) {
  return a.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;").replace(/'/g, "&#39;");
};
pagespeed.Configuration.copy_ = function(a, b) {
  var c = function() {
    b.textContent = "Copied";
    window.setTimeout(function() {
      b.textContent = "Copy";
    }, 1200);
  }, d = navigator.clipboard;
  d && d.writeText ? d.writeText(a).then(c, function() {
  }) : (d = document.createElement("textarea"), d.value = a, document.body.appendChild(d), d.select(), document.execCommand("copy"), document.body.removeChild(d), c());
};
pagespeed.Configuration.Start();
})();
