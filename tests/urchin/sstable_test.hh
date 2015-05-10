#pragma once

#include "sstables/sstables.hh"
#include "schema.hh"

static auto la = sstables::sstable::version_types::la;
static auto big = sstables::sstable::format_types::big;

namespace sstables {

using sstable_ptr = lw_shared_ptr<sstable>;

class test {
    sstable_ptr _sst;
public:

    test(sstable_ptr s) : _sst(s) {}

    summary& _summary() {
        return _sst->_summary;
    }

    future<temporary_buffer<char>> data_read(uint64_t pos, size_t len) {
        return _sst->data_read(pos, len);
    }
    future<index_list> read_indexes(uint64_t position, uint64_t quantity) {
        return _sst->read_indexes(position, quantity);
    }

    future<> read_statistics() {
        return _sst->read_statistics();
    }

    statistics& get_statistics() {
        return _sst->_statistics;
    }

    future<> read_summary() {
        return _sst->read_summary();
    }

    future<summary_entry&> read_summary_entry(size_t i) {
        return _sst->read_summary_entry(i);
    }

    summary& get_summary() {
        return _sst->_summary;
    }

    future<> read_toc() {
        return _sst->read_toc();
    }

    auto& get_components() {
        return _sst->_components;
    }

    template <typename T>
    int binary_search(const T& entries, const key& sk) {
        return _sst->binary_search(entries, sk);
    }
};

inline future<sstable_ptr> reusable_sst(sstring dir, unsigned long generation) {
    auto sst = make_lw_shared<sstable>(dir, generation, la, big);
    auto fut = sst->load();
    return std::move(fut).then([sst = std::move(sst)] {
        return make_ready_future<sstable_ptr>(std::move(sst));
    });
}

inline future<> working_sst(sstring dir, unsigned long generation) {
    return reusable_sst(dir, generation).then([] (auto ptr) { return make_ready_future<>(); });
}

inline schema_ptr composite_schema() {
    static thread_local auto s = make_lw_shared(schema({}, "tests", "composite",
        // partition key
        {{"name", bytes_type}, {"col1", bytes_type}},
        // clustering key
        {},
        // regular columns
        {},
        // static columns
        {},
        // regular column name type
        utf8_type,
        // comment
        "Table with a composite key as pkey"
       ));
    return s;
}

inline schema_ptr set_schema() {
    auto my_set_type = set_type_impl::get_instance(bytes_type, false);
    static thread_local auto s = make_lw_shared(schema({}, "tests", "set_pk",
        // partition key
        {{"ss", my_set_type}},
        // clustering key
        {},
        // regular columns
        {
            {"ns", utf8_type},
        },
        // static columns
        {},
        // regular column name type
        utf8_type,
        // comment
        "Table with a set as pkeys"
       ));
    return s;
}

inline schema_ptr map_schema() {
    auto my_map_type = map_type_impl::get_instance(bytes_type, bytes_type, false);
    static thread_local auto s = make_lw_shared(schema({}, "tests", "map_pk",
        // partition key
        {{"ss", my_map_type}},
        // clustering key
        {},
        // regular columns
        {
            {"ns", utf8_type},
        },
        // static columns
        {},
        // regular column name type
        utf8_type,
        // comment
        "Table with a map as pkeys"
       ));
    return s;
}

inline schema_ptr list_schema() {
    auto my_list_type = list_type_impl::get_instance(bytes_type, false);
    static thread_local auto s = make_lw_shared(schema({}, "tests", "list_pk",
        // partition key
        {{"ss", my_list_type}},
        // clustering key
        {},
        // regular columns
        {
            {"ns", utf8_type},
        },
        // static columns
        {},
        // regular column name type
        utf8_type,
        // comment
        "Table with a list as pkeys"
       ));
    return s;
}
}