#include <stdint.h>
#include <fcntl.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <algorithm>
#include <fstream>
#include <math.h>

#include <string>

#include "src/lib/debug.h"

#include "src/dump_load.h"
#include "src/codesearch.h"
#include "src/indexer.h"
#include "src/re_width.h"

#include <gflags/gflags.h>

using namespace std;

DEFINE_string(dot_index, "", "Write a graph of the index key as a dot graph.");
DEFINE_bool(casefold, false, "Treat the regex as case-insensitive.");

class IndexKeyDotOutputter {
protected:
    map<IndexKey*, string> names_;
    set<IndexKey*> seen_;
    ofstream out_;
    int ct_;
    intrusive_ptr<IndexKey> key_;

    string escape(char c) {
        if (c <= ' ' || c > '~' || c == '"' || c == '\\')
            return strprintf("\\\\x%hhx", c);
        return strprintf("%c", c);
    }

    void assign_names(intrusive_ptr<IndexKey> key) {
        if (names_.find(key.get()) != names_.end())
            return;
        names_[key.get()] = strprintf("node%d", ct_++);
        string flags;
        if (key->anchor & kAnchorLeft)
            flags += "^";
        if (key->anchor & kAnchorRepeat)
            flags += "*";
        if (key->anchor & kAnchorRight)
            flags += "$";

        out_ << strprintf("%s [label=\"%s\"]\n",
                          names_[key.get()].c_str(),
                          flags.c_str());
        for (auto it = key->begin(); it != key->end(); it++) {
            if (!it->second)
                continue;
            assign_names(it->second);
        }
    }

    void dump(intrusive_ptr<IndexKey> key) {
        if (seen_.find(key.get()) != seen_.end())
            return;
        seen_.insert(key.get());
        for (auto it = key->begin(); it != key->end(); it++) {
            string dst;
            if (!it->second) {
                out_ << strprintf("node%d [shape=point,label=\"\"]\n",
                                  ct_);
                dst = strprintf("node%d", ct_++);
            } else
                dst = names_[it->second.get()];
            string label;
            if (it->first.first == it->first.second)
                label = escape(it->first.first);
            else
                label = strprintf("%s-%s",
                                  escape(it->first.first).c_str(),
                                  escape(it->first.second).c_str());
            out_ << strprintf("%s -> %s [label=\"%s\"]\n",
                              names_[key.get()].c_str(),
                              dst.c_str(),
                              label.c_str());
            if (it->second)
                dump(it->second);
        }
    }

public:
    IndexKeyDotOutputter(const string &path, intrusive_ptr<IndexKey> key)
        : out_(path.c_str()), ct_(0), key_(key) {
    }

    void output() {
        out_ << "digraph G {\n";
        assign_names(key_);
        dump(key_);
        out_ << "}\n";
        out_.close();
    }
};


void write_dot_index(const string &path, intrusive_ptr<IndexKey> key) {
    IndexKeyDotOutputter out(path, key);
    out.output();
}

int analyze_re(int argc, char **argv) {
    if (argc != 1) {
        fprintf(stderr, "Usage: %s <options> REGEX\n", gflags::GetArgv0());
        return 1;
    }

    RE2::Options opts;
    default_re2_options(opts);
    if (FLAGS_casefold)
        opts.set_case_sensitive(false);

    RE2 re(argv[0], opts);
    if (!re.ok()) {
        fprintf(stderr, "Error: %s\n", re.error().c_str());
        return 1;
    }

    WidthWalker width;
    printf("== RE [%s] ==\n", argv[0]);
    printf("width: %d\n", width.Walk(re.Regexp(), 0));
    printf("Program size: %d\n", re.ProgramSize());

    intrusive_ptr<IndexKey> key = indexRE(re);
    if (key) {
        IndexKey::Stats stats = key->stats();
        printf("Index key:\n");
        printf("  log10(selectivity): %f\n", log(stats.selectivity_)/log(10));
        printf("  depth: %d\n", stats.depth_);
        printf("  nodes: %ld\n", stats.nodes_);

        if (FLAGS_dot_index.size()) {
            write_dot_index(FLAGS_dot_index, key);
        }
    } else {
        printf("(Unindexable)\n");
    }

    return 0;
}
