"use strict";
var Codesearch = function() {
  return {
    delegate: null,
    retry_time: 50,
    next_search: null,
    in_flight: null,
    connect: function(delegate) {
      if (delegate !== undefined)
        Codesearch.delegate = delegate;
      if (Codesearch.delegate.on_connect)
        setTimeout(Codesearch.delegate.on_connect, 0)
    },
    new_search: function(opts) {
      Codesearch.next_search = opts;
      if (Codesearch.in_flight == null)
        Codesearch.dispatch()
    },
    dispatch: function() {
      if (!Codesearch.next_search)
        return;
      Codesearch.in_flight = Codesearch.next_search;
      Codesearch.next_search = null;

      var opts = Codesearch.in_flight;

      var url = "/api/v1/search/"
      if ('backend' in opts) {
        url = url + opts.backend;
      }
      var q = {q: opts.q};

      url = url + "?" + $.param(q);

      var xhr = $.getJSON(url);
      var start = new Date();
      xhr.done(function (data) {
        var elapsed = new Date() - start;
        data.results.forEach(function (r) {
          Codesearch.delegate.match(opts.id, r);
        });
        data.file_results.forEach(function (r) {
          Codesearch.delegate.file_match(opts.id, r);
        });
        Codesearch.delegate.search_done(opts.id, elapsed, data.info.why);
      });
      xhr.error(function(data) {
        window._err = data;
        if (data.status >= 400 && data.status < 500) {
          var err = JSON.parse(data.responseText);
          Codesearch.delegate.error(opts.id, err.error.message);
        } else {
          console.log("server error", data.status, data.responseText);
        }
      });
      xhr.always(function() {
        Codesearch.in_flight = null;
        setTimeout(Codesearch.dispatch, 0);
      });
    }
  };
}();
