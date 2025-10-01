/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * dwarfs is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * dwarfs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with dwarfs.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <sstream>
#include <string>
#include <unordered_map>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/binary_literals.h>

#include <dwarfs/internal/os_access_generic_data.h>

using namespace dwarfs::binary_literals;
using dwarfs::internal::os_access_generic_data;

namespace {

class test_env {
 public:
  void set(std::string const& var, std::string const& value) {
    vars_[var] = value;
  }

  char const* operator()(char const* var) const {
    auto const it = vars_.find(var);
    return it == vars_.end() ? nullptr : it->second.c_str();
  }

 private:
  std::unordered_map<std::string, std::string> vars_;
};

} // namespace

TEST(os_access_generic_data, empty_environment) {
  test_env env;
  std::ostringstream err;

  os_access_generic_data data{err, env};

  EXPECT_TRUE(err.str().empty());

  if (sizeof(void*) == 4) {
    EXPECT_EQ(data.fv_opts().max_eager_map_size, 32_MiB);
  } else {
    EXPECT_FALSE(data.fv_opts().max_eager_map_size.has_value());
  }
}

TEST(os_access_generic_data, valid_max_eager_map_size) {
  test_env env;
  env.set("DWARFS_IOLAYER_OPTS", "max_eager_map_size=64M");
  std::ostringstream err;

  os_access_generic_data data{err, env};

  EXPECT_TRUE(err.str().empty());
  EXPECT_EQ(data.fv_opts().max_eager_map_size, 64_MiB);
}

TEST(os_access_generic_data, valid_max_eager_map_size_unlimited) {
  test_env env;
  env.set("DWARFS_IOLAYER_OPTS", "max_eager_map_size=unlimited");
  std::ostringstream err;

  os_access_generic_data data{err, env};

  EXPECT_TRUE(err.str().empty());
  // regardless of architecture
  EXPECT_FALSE(data.fv_opts().max_eager_map_size.has_value());
}

TEST(os_access_generic_data, invalid_max_eager_map_size) {
  test_env env;
  env.set("DWARFS_IOLAYER_OPTS", "max_eager_map_size=123foo");
  std::ostringstream err;

  os_access_generic_data data{err, env};

  if (sizeof(void*) == 4) {
    EXPECT_EQ(data.fv_opts().max_eager_map_size, 32_MiB);
  } else {
    EXPECT_FALSE(data.fv_opts().max_eager_map_size.has_value());
  }

  EXPECT_THAT(err.str(), testing::HasSubstr(
                             "warning: ignoring invalid DWARFS_IOLAYER_OPTS "
                             "option 'max_eager_map_size'"));
}

TEST(os_access_generic_data, unknown_option) {
  test_env env;
  env.set("DWARFS_IOLAYER_OPTS", "foo=bar");
  std::ostringstream err;

  os_access_generic_data data{err, env};

  if (sizeof(void*) == 4) {
    EXPECT_EQ(data.fv_opts().max_eager_map_size, 32_MiB);
  } else {
    EXPECT_FALSE(data.fv_opts().max_eager_map_size.has_value());
  }

  EXPECT_THAT(
      err.str(),
      testing::HasSubstr(
          "warning: ignoring unknown DWARFS_IOLAYER_OPTS option 'foo'"));
}

TEST(os_access_generic_data, extra_options) {
  test_env env;
  env.set("DWARFS_IOLAYER_OPTS", "foo=bar,max_eager_map_size=64K,someflag");
  std::ostringstream err;

  os_access_generic_data data{err, env};

  EXPECT_EQ(data.fv_opts().max_eager_map_size, 64_KiB);

  EXPECT_THAT(
      err.str(),
      testing::HasSubstr(
          "warning: ignoring unknown DWARFS_IOLAYER_OPTS option 'foo'"));

  EXPECT_THAT(
      err.str(),
      testing::HasSubstr(
          "warning: ignoring unknown DWARFS_IOLAYER_OPTS option 'someflag'"));
}
