/*
 * Copyright (C) 2015 Cloudius Systems, Ltd.
 */

#pragma once

#include "bytes.hh"
#include "timestamp.hh"
#include <cstdint>

template<typename T>
static inline
void set_field(bytes& v, unsigned offset, T val) {
    reinterpret_cast<net::packed<T>*>(v.begin() + offset)->raw = net::hton(val);
}

template<typename T>
static inline
T get_field(const bytes_view& v, unsigned offset) {
    return net::ntoh(*reinterpret_cast<const net::packed<T>*>(v.begin() + offset));
}

class atomic_cell_or_collection;

/*
 * Represents atomic cell layout. Works on serialized form.
 *
 * Layout:
 *
 *  <live>  := <int8_t:flags><int64_t:timestamp><int32_t:ttl>?<value>
 *  <dead>  := <int8_t:    0><int64_t:timestamp><int32_t:ttl>
 */
class atomic_cell final {
private:
    static constexpr int8_t DEAD_FLAGS = 0;
    static constexpr int8_t LIVE_FLAG = 0x01;
    static constexpr int8_t TTL_FLAG  = 0x02; // When present, TTL field is present. Set only for live cells
    static constexpr unsigned flags_size = 1;
    static constexpr unsigned timestamp_offset = flags_size;
    static constexpr unsigned timestamp_size = 8;
    static constexpr unsigned ttl_offset = timestamp_offset + timestamp_size;
    static constexpr unsigned ttl_size = 4;
public:
    class view;
    class one;
private:
    static bool is_live(const bytes_view& cell) {
        return cell[0] != DEAD_FLAGS;
    }
    static bool is_live_and_has_ttl(const bytes_view& cell) {
        return cell[0] & TTL_FLAG;
    }
    static bool is_dead(const bytes_view& cell) {
        return cell[0] == DEAD_FLAGS;
    }
    // Can be called on live and dead cells
    static api::timestamp_type timestamp(const bytes_view& cell) {
        return get_field<api::timestamp_type>(cell, timestamp_offset);
    }
    // Can be called on live cells only
    static bytes_view value(bytes_view cell) {
        auto ttl_field_size = bool(cell[0] & TTL_FLAG) * ttl_size;
        auto value_offset = flags_size + timestamp_size + ttl_field_size;
        cell.remove_prefix(value_offset);
        return cell;
    }
    // Can be called on live and dead cells. For dead cells, the result is never empty.
    static ttl_opt ttl(const bytes_view& cell) {
        auto flags = cell[0];
        if (flags == DEAD_FLAGS || (flags & TTL_FLAG)) {
            auto ttl = get_field<int32_t>(cell, ttl_offset);
            return {gc_clock::time_point(gc_clock::duration(ttl))};
        }
        return {};
    }
    static bytes make_dead(api::timestamp_type timestamp, gc_clock::time_point ttl) {
        bytes b(bytes::initialized_later(), flags_size + timestamp_size + ttl_size);
        b[0] = DEAD_FLAGS;
        set_field(b, timestamp_offset, timestamp);
        set_field(b, ttl_offset, ttl.time_since_epoch().count());
        return b;
    }
    static bytes make_live(api::timestamp_type timestamp, ttl_opt ttl, bytes_view value) {
        auto value_offset = flags_size + timestamp_size + bool(ttl) * ttl_size;
        bytes b(bytes::initialized_later(), value_offset + value.size());
        b[0] = (ttl ? TTL_FLAG : 0) | LIVE_FLAG;
        set_field(b, timestamp_offset, timestamp);
        if (ttl) {
            set_field(b, ttl_offset, ttl->time_since_epoch().count());
        }
        std::copy_n(value.begin(), value.size(), b.begin() + value_offset);
        return b;
    }
    friend class one;
    friend class view;
};

class atomic_cell::view final {
    bytes_view _data;
private:
    view(bytes_view data) : _data(data) {}
public:
    static view from_bytes(bytes_view data) { return view(data); }
    bool is_live() const {
        return atomic_cell::is_live(_data);
    }
    bool is_live_and_has_ttl() const {
        return atomic_cell::is_live_and_has_ttl(_data);
    }
    bool is_dead() const {
        return atomic_cell::is_dead(_data);
    }
    // Can be called on live and dead cells
    api::timestamp_type timestamp() const {
        return atomic_cell::timestamp(_data);
    }
    // Can be called on live cells only
    bytes_view value() const {
        return atomic_cell::value(_data);
    }
    // Can be called on live and dead cells. For dead cells, the result is never empty.
    ttl_opt ttl() const {
        return atomic_cell::ttl(_data);
    }
    friend class atomic_cell::one;
};

class atomic_cell::one final {
    bytes _data;
private:
    one(bytes b) : _data(std::move(b)) {}
public:
    one(const one&) = default;
    one(one&&) = default;
    one& operator=(const one&) = default;
    one& operator=(one&&) = default;
    static one from_bytes(bytes b) {
        return one(std::move(b));
    }
    one(atomic_cell::view other) : _data(other._data.begin(), other._data.end()) {}
    bool is_live() const {
        return atomic_cell::is_live(_data);
    }
    bool is_live_and_has_ttl() const {
        return atomic_cell::is_live_and_has_ttl(_data);
    }
    bool is_dead(const bytes_view& cell) const {
        return atomic_cell::is_dead(_data);
    }
    // Can be called on live and dead cells
    api::timestamp_type timestamp() const {
        return atomic_cell::timestamp(_data);
    }
    // Can be called on live cells only
    bytes_view value() const {
        return atomic_cell::value(_data);
    }
    // Can be called on live and dead cells. For dead cells, the result is never empty.
    ttl_opt ttl() const {
        return atomic_cell::ttl(_data);
    }
    operator atomic_cell::view() const {
        return atomic_cell::view(_data);
    }
    static one make_dead(api::timestamp_type timestamp, gc_clock::time_point ttl) {
        return atomic_cell::make_dead(timestamp, ttl);
    }
    static one make_live(api::timestamp_type timestamp, ttl_opt ttl, bytes_view value) {
        return atomic_cell::make_live(timestamp, ttl, value);
    }
    friend class atomic_cell_or_collection;
};

// A variant type that can hold either an atomic_cell, or a serialized collection.
// Which type is stored is determinied by the schema.
class atomic_cell_or_collection final {
    bytes _data;
private:
    atomic_cell_or_collection(bytes&& data) : _data(std::move(data)) {}
public:
    static atomic_cell_or_collection from_atomic_cell(atomic_cell::one data) { return { std::move(data._data) }; }
    atomic_cell::view as_atomic_cell() const { return atomic_cell::view::from_bytes(_data); }
    // FIXME: insert collection variant here
};
