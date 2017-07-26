/********************************************************************
 * livegrep -- dump_load.cc
 * Copyright (c) 2011-2013 Nelson Elhage
 *
 * This program is free software. You may use, redistribute, and/or
 * modify it under the terms listed in the COPYING file.
 ********************************************************************/
#include "src/codesearch.h"
#include "src/chunk.h"
#include "src/chunk_allocator.h"
#include "src/content.h"
#include "src/dump_load.h"

#include <map>
#include <string>
#include <memory>

#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <json-c/json.h>

class codesearch_index {
public:
    codesearch_index(code_searcher *cs, string path) :
        cs_(cs),
        stream_(path.c_str(), ios::out | ios::trunc),
        hdr_() {
        assert(!stream_.fail());
        fd_ = open(path.c_str(), O_RDWR|O_APPEND);
        assert(fd_ > 0);

        hdr_.magic      = kIndexMagic;
        hdr_.version    = kIndexVersion;
        hdr_.chunk_size = cs->alloc_->chunk_size();
    }

    ~codesearch_index() {
        close(fd_);
    }

    void dump();
protected:
    void dump_chunk_data();
    void dump_metadata();
    void dump_file(map<const indexed_tree*, int>& ids, indexed_file *sf);
    void dump_chunk_file(chunk_file *cf);
    void dump_chunk_files(chunk *, chunk_header *);
    void dump_chunk_data(chunk *);
    void dump_content_data();

    void alignp(uint32_t align) {
        streampos pos = stream_.tellp();
        stream_.seekp((size_t(pos) + align - 1) & ~(align - 1));
    }

    template<class T>
    void dump(T *t) {
        stream_.write(reinterpret_cast<char*>(t), sizeof *t);
    }

    void dump_int32(uint32_t i) {
        dump(&i);
    }

    void dump_string(const string &str) {
        dump_int32(str.size());
        stream_.write(str.c_str(), str.size());
    }

    code_searcher *cs_;
    std::fstream stream_;
    int fd_;

    uint8_t *map_;
    uint8_t *p_;

    index_header hdr_;
    vector<chunk_header> chunks_;
    vector<content_chunk_header> content_;

    friend class dump_allocator;
};

class dump_allocator : public chunk_allocator {
private:
    pair<off_t, uint8_t *> alloc_mmap(size_t len) {
        void *buf;

        if (!index_.get()) {
            index_.reset(new codesearch_index(cs_, path_.c_str()));
            index_->dump(&index_->hdr_);
            index_->alignp(kPageSize);
        }

        off_t off = index_->stream_.tellp();
        assert(ftruncate(index_->fd_, off + len) == 0);
        buf = mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_SHARED,
                   index_->fd_, off);
        assert(buf != MAP_FAILED);
        index_->stream_.seekp(len, ios::cur);
        return make_pair(off, static_cast<uint8_t*>(buf));
    }

public:
    dump_allocator(code_searcher *cs, const char *path)
        : cs_(cs), path_(path), index_() {
    }

    virtual chunk *alloc_chunk() {
        auto alloc = alloc_mmap((1 + sizeof(uint32_t)) * chunk_size_);

        chunk_header chdr = {
            uint64_t(alloc.first)
        };
        index_->chunks_.push_back(chdr);

        return new chunk(static_cast<unsigned char*>(alloc.second),
                         reinterpret_cast<uint32_t*>
                         (static_cast<unsigned char*>(alloc.second) + chunk_size_));
    }

    virtual buffer alloc_content_chunk() {
        auto alloc = alloc_mmap(kContentChunkSize);
        buffer b = {
            alloc.second, alloc.second + kContentChunkSize
        };
        content_chunk_header hdr = {
            uint64_t(alloc.first)
            /* .size will be calculated in finalize */
        };
        index_->content_.push_back(hdr);
        return b;
    }

    virtual void finalize() {
        chunk_allocator::finalize();
        auto cit = index_->content_.begin();
        for (auto ait = begin_content();
             ait != end_content(); ++ait, ++cit) {
            cit->size = ait->end - ait->data;
        }
        index_->dump_metadata();
        index_->stream_.seekp(0);
        index_->dump(&index_->hdr_);
        index_->stream_.close();
    }

    virtual void free_chunk(chunk *chunk) {
        munmap(chunk->data, 5*chunk_size_);
        delete chunk;
    }
protected:
    code_searcher *cs_;
    std::string path_;
    unique_ptr<codesearch_index> index_;
    map<void *, off_t> alloc_map_;
    vector<off_t> content_;

};

class load_allocator : public chunk_allocator {
public:
    load_allocator(code_searcher *cs, const string& path);

    ~load_allocator() {
        close(fd_);
        munmap(map_, map_size_);
    }

    virtual chunk *alloc_chunk();
    virtual buffer alloc_content_chunk() {
        assert(0);
    }

    virtual void free_chunk(chunk *chunk) {
        delete chunk;
    }

    virtual void drop_caches() {
        for (auto it = begin(); it != end(); ++it) {
            madvise((*it)->data, (*it)->size, MADV_DONTNEED);
            madvise((*it)->suffixes, (*it)->size * sizeof(*(*it)->suffixes), MADV_DONTNEED);
        }
#ifdef POSIX_FADV_DONTNEED
        posix_fadvise(fd_, hdr_->chunks_off,
                      chunks_.size() * chunk_size_ * (1 + sizeof(uint32_t)),
                      POSIX_FADV_DONTNEED);
#endif
    }

    void load(code_searcher *cs);
protected:
    template <class T>
    T *consume() {
        T *out = reinterpret_cast<T*>(p_);
        p_ += sizeof(T);
        return out;
    }

    template <class T>
    T *ptr(uint64_t off) {
        assert(off < map_size_);
        return reinterpret_cast<T*>(static_cast<uint8_t*>(map_) + off);
    }

    void seekg(off_t off) {
        p_ = static_cast<uint8_t*>(map_) + off;
    }

    indexed_file *load_file(code_searcher *cs);
    void load_chunk(code_searcher *);

    uint32_t load_int32() {
        return *(consume<uint32_t>());
    }

    string load_string() {
        uint32_t len = load_int32();
        uint8_t *buf = p_;
        p_ += len;
        return string(reinterpret_cast<char*>(buf), len);
    }

    int fd_;
    void *map_;
    size_t map_size_;
    uint8_t *p_;

    index_header *hdr_;
    chunk_header *chunks_hdr_;
    chunk_header *next_chunk_;
};

chunk_allocator *make_dump_allocator(code_searcher *search, const string& path) {
    return new dump_allocator(search, path.c_str());
}

void codesearch_index::dump_file(map<const indexed_tree*, int>& ids, indexed_file *sf) {
    dump_int32(ids[sf->tree]);
    dump_string(sf->path);
}

void codesearch_index::dump_chunk_file(chunk_file *cf) {
    dump_int32(cf->files.size());
    for (list<indexed_file*>::iterator it = cf->files.begin();
         it != cf->files.end(); ++it)
        dump_int32((*it)->no);

    dump_int32(cf->left);
    dump_int32(cf->right);
}

void codesearch_index::dump_chunk_files(chunk *chunk, chunk_header *hdr) {
    hdr->files_off = stream_.tellp();
    hdr->nfiles = chunk->files.size();
    hdr->size = chunk->size;

    for (vector<chunk_file>::iterator it = chunk->files.begin();
         it != chunk->files.end(); it ++)
        dump_chunk_file(&(*it));
}

void codesearch_index::dump_chunk_data(chunk *chunk) {
    alignp(kPageSize);
    size_t off = stream_.tellp();

    chunk_header chdr;
    chdr.data_off = off;
    chdr.size = chunk->size;
    chunks_.push_back(chdr);

    assert(ftruncate(fd_, off + 5 * hdr_.chunk_size) == 0);
    stream_.write(reinterpret_cast<char*>(chunk->data), hdr_.chunk_size);
    stream_.write(reinterpret_cast<char*>(chunk->suffixes),
                  sizeof(uint32_t) * chunk->size);
    stream_.seekp(off + 5 * hdr_.chunk_size);
}

void codesearch_index::dump_metadata() {
    hdr_.ntrees   = cs_->trees_.size();
    hdr_.nfiles   = cs_->files_.size();
    hdr_.nchunks  = cs_->alloc_->size();
    hdr_.ncontent = content_.size();

    hdr_.name_off = stream_.tellp();
    dump_string(cs_->name());

    map<const indexed_tree*, int> tree_ids;

    hdr_.refs_off = stream_.tellp();
    for (auto it = cs_->trees_.begin();
         it != cs_->trees_.end(); ++it) {
        dump_string((*it)->name);
        dump_string((*it)->version);
        if ((*it)->metadata)
            dump_string(json_object_to_json_string((*it)->metadata));
        else
            dump_string("");
        tree_ids[*it] = it - cs_->trees_.begin();
    }
    hdr_.files_off = stream_.tellp();
    for (vector<indexed_file*>::iterator it = cs_->files_.begin();
         it != cs_->files_.end(); ++it)
        dump_file(tree_ids, *it);

    auto hdr = chunks_.begin();
    for (auto it = cs_->alloc_->begin();
         it != cs_->alloc_->end(); ++it, ++hdr) {
        assert(hdr != chunks_.end());
        dump_chunk_files(*it, &(*hdr));
    }

    hdr_.chunks_off = stream_.tellp();
    for (auto it = chunks_.begin(); it != chunks_.end(); ++it)
        dump(&*it);

    hdr_.content_off = stream_.tellp();
    for (auto it = content_.begin(); it != content_.end(); ++it)
        dump(&*it);
}

void codesearch_index::dump_chunk_data() {
    alignp(kPageSize);
    for (auto it = cs_->alloc_->begin();
         it != cs_->alloc_->end(); ++it) {
        dump_chunk_data(*it);
    }
}

void codesearch_index::dump_content_data() {
    alignp(kPageSize);
    for (auto it = cs_->alloc_->begin_content();
         it != cs_->alloc_->end_content(); ++it) {
        off_t off = stream_.tellp();
        stream_.write(reinterpret_cast<char*>(it->data), it->end - it->data);
        content_.push_back((content_chunk_header) {
                uint64_t(off),
                uint32_t(it->end - it->data)
            });
    }
}

void codesearch_index::dump() {
    assert(cs_->finalized_);

    dump(&hdr_);

    dump_chunk_data();
    dump_content_data();
    dump_metadata();

    stream_.seekp(0);
    dump(&hdr_);
}

load_allocator::load_allocator(code_searcher *cs, const string& path) {
    fd_ = open(path.c_str(), O_RDONLY);
    assert(fd_ > 0);
    struct stat st;
    assert(fstat(fd_, &st) == 0);
    map_size_ = st.st_size;
    map_ = mmap(NULL, map_size_, PROT_READ, MAP_SHARED,
                fd_, 0);
    assert(map_ != MAP_FAILED);
    p_ = static_cast<unsigned char*>(map_);

    hdr_ = consume<index_header>();
    set_chunk_size(hdr_->chunk_size);
    chunks_hdr_ = next_chunk_ = ptr<chunk_header>(hdr_->chunks_off);

    p_ = ptr<unsigned char>(hdr_->name_off);
    cs->set_name(load_string());
}


chunk *load_allocator::alloc_chunk() {
    unsigned char *data = ptr<unsigned char>(next_chunk_->data_off);
    uint32_t *indexes = reinterpret_cast<uint32_t*>(data + chunk_size_);

    return new chunk(data, indexes);
}

indexed_file *load_allocator::load_file(code_searcher *cs) {
    indexed_file *sf = new indexed_file;
    sf->tree = cs->trees_[load_int32()];
    sf->path = load_string();
    sf->no = cs->files_.size();
    return sf;
}

void load_allocator::load_chunk(code_searcher *cs) {
    skip_chunk();
    chunk* chunk = current_chunk();

    assert(next_chunk_->size <= hdr_->chunk_size);
    chunk->size = next_chunk_->size;

    p_ = ptr<unsigned char>(next_chunk_->files_off);

    for (int i = 0; i < next_chunk_->nfiles; i++) {
        chunk->files.push_back(chunk_file());
        chunk_file &cf = chunk->files.back();
        uint32_t nfiles = load_int32();
        for (int j = 0; j < nfiles; j++)
            cf.files.push_back(cs->files_[load_int32()]);
        cf.left  = load_int32();
        cf.right = load_int32();
    }
    chunk->build_tree();
    ++next_chunk_;
}

void load_allocator::load(code_searcher *cs) {
    assert(!cs->finalized_);
    assert(!cs->trees_.size());

    assert(hdr_->magic == kIndexMagic);
    assert(hdr_->version == kIndexVersion);
    assert(hdr_->chunks_off);

    set_chunk_size(hdr_->chunk_size);

    p_ = ptr<uint8_t>(hdr_->refs_off);
    for (int i = 0; i < hdr_->ntrees; i++) {
        indexed_tree *tree = new indexed_tree;
        tree->name = load_string();
        tree->version = load_string();
        string metadata = load_string();
        if (metadata.size() == 0) {
            tree->metadata = NULL;
        } else {
            json_object *js = json_tokener_parse(metadata.c_str());
            assert(!is_error(js));
            tree->metadata = js;
        }

        cs->trees_.push_back(tree);
    }

    p_ = ptr<uint8_t>(hdr_->files_off);
    for (int i = 0; i < hdr_->nfiles; i++) {
        cs->files_.push_back(load_file(cs));
    }

    assert(!current_);
    for (int i = 0; i < hdr_->nchunks; i++) {
        load_chunk(cs);
    }

    content_chunk_header *chdr = ptr<content_chunk_header>(hdr_->content_off);
    auto it = cs->files_.begin();
    for (int i = 0; i < hdr_->ncontent; i++) {
        buffer b;
        p_ = ptr<uint8_t>(chdr->file_off);
        b.data = p_;
        while (p_ < ptr<uint8_t>(chdr->file_off + chdr->size)) {
            (*it)->content = new(p_) file_contents;
            p_ = reinterpret_cast<uint8_t*>((*it)->content->end());
            ++it;
        }
        b.end = p_;
        content_chunks_.push_back(b);
        ++chdr;
    }
    assert(it == cs->files_.end());

    struct stat st;
    assert(fstat(fd_, &st) == 0);
    cs->index_timestamp_ = st.st_mtime;

    cs->index_filenames();

    cs->finalized_ = true;
}

void code_searcher::dump_index(const string &path) {
    codesearch_index idx(this, path);
    idx.dump();
}

void code_searcher::load_index(const string &path) {
    load_allocator *alloc = new load_allocator(this, path);
    set_alloc(alloc);
    alloc->load(this);
}
