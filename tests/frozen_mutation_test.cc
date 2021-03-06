/*
 * Copyright 2015 Cloudius Systems
 */

/*
 * This file is part of Scylla.
 *
 * Scylla is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Scylla is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Scylla.  If not, see <http://www.gnu.org/licenses/>.
 */

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE core

#include <boost/test/unit_test.hpp>

#include "frozen_mutation.hh"
#include "schema_builder.hh"
#include "tests/mutation_assertions.hh"

static schema_builder new_table() {
    return { "some_keyspace", "some_table" };
}

static api::timestamp_type new_timestamp() {
    static api::timestamp_type t = 0;
    return t++;
};

static tombstone new_tombstone() {
    return { new_timestamp(), gc_clock::now() };
};

BOOST_AUTO_TEST_CASE(test_writing_and_reading) {
    schema_ptr s = new_table()
        .with_column("pk_col", bytes_type, column_kind::partition_key)
        .with_column("ck_col_1", bytes_type, column_kind::clustering_key)
        .with_column("ck_col_2", bytes_type, column_kind::clustering_key)
        .with_column("regular_col_1", bytes_type)
        .with_column("regular_col_2", bytes_type)
        .with_column("static_col_1", bytes_type, column_kind::static_column)
        .with_column("static_col_2", bytes_type, column_kind::static_column)
        .build();

    partition_key key = partition_key::from_single_value(*s, bytes("key"));
    clustering_key ck1 = clustering_key::from_deeply_exploded(*s, {data_value(bytes("ck1_0")), data_value(bytes("ck1_1"))});
    clustering_key ck2 = clustering_key::from_deeply_exploded(*s, {data_value(bytes("ck2_0")), data_value(bytes("ck2_1"))});
    auto ttl = gc_clock::duration(1);

    auto test_freezing = [] (const mutation& m) {
        assert_that(freeze(m).unfreeze(m.schema())).is_equal_to(m);
    };

    mutation m(key, s);
    m.partition().apply(new_tombstone());

    test_freezing(m);

    m.partition().apply_delete(*s, ck2, new_tombstone());

    test_freezing(m);

    m.partition().apply_row_tombstone(*s, clustering_key_prefix::from_deeply_exploded(*s, {data_value(bytes("ck2_0"))}), new_tombstone());

    test_freezing(m);

    m.set_clustered_cell(ck1, "regular_col_1", data_value(bytes("regular_col_value")), new_timestamp(), ttl);

    test_freezing(m);

    m.set_clustered_cell(ck1, "regular_col_2", data_value(bytes("regular_col_value")), new_timestamp(), ttl);

    test_freezing(m);

    m.partition().apply_insert(*s, ck2, new_timestamp());

    test_freezing(m);

    m.set_clustered_cell(ck2, "regular_col_1", data_value(bytes("ck2_regular_col_1_value")), new_timestamp());

    test_freezing(m);

    m.set_static_cell("static_col_1", data_value(bytes("static_col_value")), new_timestamp(), ttl);

    test_freezing(m);

    m.set_static_cell("static_col_2", data_value(bytes("static_col_value")), new_timestamp());

    test_freezing(m);

    {
        auto fm = freeze(m);
        auto dk = fm.decorated_key(*s);
        BOOST_REQUIRE(dk.equal(*s, m.decorated_key()));
    }
}

BOOST_AUTO_TEST_CASE(test_application_of_partition_view_has_the_same_effect_as_applying_regular_mutation) {
    schema_ptr s = new_table()
        .with_column("pk_col", bytes_type, column_kind::partition_key)
        .with_column("ck_1", bytes_type, column_kind::clustering_key)
        .with_column("reg_1", bytes_type)
        .with_column("reg_2", bytes_type)
        .with_column("static_1", bytes_type, column_kind::static_column)
        .build();

    partition_key key = partition_key::from_single_value(*s, bytes("key"));
    clustering_key ck = clustering_key::from_deeply_exploded(*s, {data_value(bytes("ck"))});

    mutation m1(key, s);
    m1.partition().apply(new_tombstone());
    m1.set_clustered_cell(ck, "reg_1", data_value(bytes("val1")), new_timestamp());
    m1.set_clustered_cell(ck, "reg_2", data_value(bytes("val2")), new_timestamp());
    m1.partition().apply_insert(*s, ck, new_timestamp());
    m1.set_static_cell("static_1", data_value(bytes("val3")), new_timestamp());

    mutation m2(key, s);
    m2.set_clustered_cell(ck, "reg_1", data_value(bytes("val4")), new_timestamp());
    m2.partition().apply_insert(*s, ck, new_timestamp());
    m2.set_static_cell("static_1", data_value(bytes("val5")), new_timestamp());

    mutation m_frozen(key, s);
    m_frozen.partition().apply(*s, freeze(m1).partition());
    m_frozen.partition().apply(*s, freeze(m2).partition());

    mutation m_unfrozen(key, s);
    m_unfrozen.partition().apply(*s, m1.partition());
    m_unfrozen.partition().apply(*s, m2.partition());

    mutation m_refrozen(key, s);
    m_refrozen.partition().apply(*s, freeze(m1).unfreeze(s).partition());
    m_refrozen.partition().apply(*s, freeze(m2).unfreeze(s).partition());

    assert_that(m_unfrozen).is_equal_to(m_refrozen);
    assert_that(m_unfrozen).is_equal_to(m_frozen);
}
