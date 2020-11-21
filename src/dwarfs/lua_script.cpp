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
 */

#include "lua_script.h"

#include <iostream>
#include <unordered_map>

#include <luabind/luabind.hpp>

#include "dwarfs/logger.h"

extern "C" {
#include <lualib.h>
}

namespace dwarfs {

class lua_script::impl {
 public:
  impl(logger& lgr, const std::string& file);
  ~impl();

  bool filter(file_interface const& fi) const;
  void order(file_vector& fv) const;

 private:
  lua_State* L_;
  log_proxy<debug_logger_policy> log_;
};

lua_script::impl::impl(logger& lgr, const std::string& file)
    : L_(luaL_newstate())
    , log_(lgr) {
  luabind::open(L_);

  luabind::module(L_)[luabind::class_<file_interface>("file_interface")
                          .property("path", &file_interface::path)
                          .property("name", &file_interface::name)
                          .property("size", &file_interface::size)
                          .property("type", &file_interface::type_string)];

  luaL_openlibs(L_);
  luaL_dofile(L_, file.c_str());
}

lua_script::impl::~impl() { lua_close(L_); }

bool lua_script::impl::filter(file_interface const& fi) const {
  return luabind::call_function<bool>(L_, "filter", &fi);
}

void lua_script::impl::order(file_vector& fv) const {
  luabind::object in = luabind::newtable(L_);

  for (size_t i = 0; i < fv.size(); ++i) {
    in[i + 1] = fv[i];
  }

  log_.info() << "ordering " << fv.size() << " entries...";

  luabind::object out;

  {
    auto ti = log_.timed_info();
    out = luabind::call_function<luabind::object>(L_, "order", in);
    ti << fv.size() << " entries ordered";
  }

  if (luabind::type(out) != LUA_TTABLE) {
    // TODO: better error handling
    throw std::runtime_error("unexpected result type");
  }

  std::unordered_map<const file_interface*, size_t> vmap;

  for (luabind::iterator i(out), end; i != end; ++i) {
    size_t key = luabind::object_cast<size_t>(i.key()) - 1;
    auto val = luabind::object_cast<const file_interface*>(*i);
    vmap[val] = key;
  }

  fv.sort([&](const file_interface* a, const file_interface* b) {
    return vmap.at(a) < vmap.at(b);
  });
}

lua_script::lua_script(logger& lgr, const std::string& file)
    : impl_(new impl(lgr, file)) {}

lua_script::~lua_script() = default;

bool lua_script::filter(file_interface const& fi) const {
  return impl_->filter(fi);
}

void lua_script::order(file_vector& fv) const { impl_->order(fv); }
} // namespace dwarfs
