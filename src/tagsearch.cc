/********************************************************************
 * livegrep -- tagsearch.cc
 * Copyright (c) 2011-2013 Nelson Elhage
 *
 * This program is free software. You may use, redistribute, and/or
 * modify it under the terms listed in the COPYING file.
 ********************************************************************/
#include "src/tagsearch.h"
#include "src/content.h"

#include "src/lib/debug.h"

#include <utility>
#include <boost/filesystem.hpp>

using re2::RE2;
using boost::filesystem::path;

namespace {

std::string create_partial_regex(RE2 *re) {
    if (!re)
        return ".*";

    std::string pattern = re->pattern();

    if (pattern.front() == '^')
        pattern.erase(pattern.begin());
    else
        pattern.insert(0, ".*");

    if (pattern.back() == '$')
        pattern.erase(pattern.size() - 1);
    else
        pattern.append(".*");

    return pattern;
}

std::string create_tag_line_regex(
    const std::string& name,
    const std::string& file,
    const std::string& lno,
    const std::string& tags) {
    // full regex match for a tag line created with
    //  ctags --format=2 -n --fields=+K
    return std::string("^") + name + "\t" + file + "\t" + lno + ";\"\t" + tags + "$";
}

};

void tag_searcher::cache_indexed_files(code_searcher* cs) {
    file_alloc_ = cs->alloc_;
    for (auto it = cs->begin_files(); it != cs->end_files(); ++it) {
        auto file = *it;
        auto key = path(file->tree->name) / path(file->path);
        path_to_file_map_.insert(std::make_pair(key.string(), file));
    }
}

bool tag_searcher::transform(query *q, match_result *m) const {
    static const std::string regex = create_tag_line_regex("(.+)", "(.+)", "(\\d+)", "(.+)");
    StringPiece name, tags_path, tags;
    if (!RE2::FullMatch(m->line, regex, &name, &tags_path, &m->lno, &tags)) {
        log(q->trace_id, "unknown ctags format: %s\n", m->line.as_string().c_str());
        return false;
    }

    // check the negation constraints
    if (q->negate.file_pat &&
        q->negate.file_pat->Match(tags_path, 0, tags_path.size(), RE2::UNANCHORED, NULL, 0))
        return false;
    if (q->negate.tags_pat &&
        q->negate.tags_pat->Match(tags, 0, tags.size(), RE2::UNANCHORED, NULL, 0))
        return false;

    // lookup the indexed_file base on repo and path
    path lookup = path(m->file->tree->name) /
        path(m->file->path).parent_path() /
        path(tags_path.as_string());
    auto value = path_to_file_map_.find(lookup.string());
    if (value == path_to_file_map_.end()) {
        log(q->trace_id,
            "unable to find a file matching %s\n",
            lookup.string().c_str());
        return false;
    }
    auto file = value->second;

    // iterate through the lines to add context information
    auto line_it = file->content->begin(file_alloc_);
    auto line_end = file->content->end(file_alloc_);
    const int kContextLines = 3;
    m->file = file;

    // jump to context before
    int current = 1;
    for (;current < std::max(1, m->lno - kContextLines); ++current)
        ++line_it;

    // context before (we reverse the order to match codesearch)
    m->context_before.clear();
    for (; current < m->lno; ++current) {
        m->context_before.insert(m->context_before.begin(), *line_it);
        ++line_it;
    }


    // line (match the first occurrence for simplicity)
    m->line = *line_it;
    m->matchleft = line_it->find(name);
    m->matchright = m->matchleft + name.size();
    ++line_it;

    // context after
    m->context_after.clear();
    for (int i = 0; i < kContextLines && line_it != line_end; ++i) {
        m->context_after.push_back(*line_it);
        ++line_it;
    }

    return true;
}

std::string tag_searcher::create_tag_line_regex_from_query(query *q) {
    return create_tag_line_regex(create_partial_regex(q->line_pat.get()),
                                 create_partial_regex(q->file_pat.get()),
                                 "\\d+",
                                 create_partial_regex(q->tags_pat.get()));
}
