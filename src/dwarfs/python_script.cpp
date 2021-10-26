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

#include <chrono>
#include <unordered_set>

#include <boost/algorithm/string.hpp>
#include <boost/python.hpp>

#include <fmt/format.h>

#include <folly/container/F14Map.h>

#include "dwarfs/entry.h"
#include "dwarfs/error.h"
#include "dwarfs/inode.h"
#include "dwarfs/logger.h"
#include "dwarfs/options_interface.h"
#include "dwarfs/python_script.h"

namespace dwarfs {

namespace py = boost::python;

namespace {

std::unordered_set<std::string> supported_methods{"configure", "filter",
                                                  "transform", "order"};

void init_python() {
  static bool initialized = false;
  if (!initialized) {
    Py_Initialize();
    initialized = true;
  }
}

bool callable(py::object object) { return 1 == PyCallable_Check(object.ptr()); }

bool hasattr(py::object obj, const char* name) {
  return PyObject_HasAttrString(obj.ptr(), name);
}

bool has_callable(py::object obj, char const* method) {
  return hasattr(obj, method) && callable(obj.attr(method));
}

class py_logger {
 public:
  explicit py_logger(logger& lgr)
      : log_(lgr) {}

  void error(std::string const& msg) { LOG_ERROR << "[script] " << msg; }
  void warn(std::string const& msg) { LOG_WARN << "[script] " << msg; }
  void info(std::string const& msg) { LOG_INFO << "[script] " << msg; }
  void debug(std::string const& msg) { LOG_DEBUG << "[script] " << msg; }
  void trace(std::string const& msg) { LOG_TRACE << "[script] " << msg; }

 private:
  using log_proxy_t = log_proxy<debug_logger_policy>;
  log_proxy_t log_;
};

template <typename T>
class basic_entry_wrapper {
 public:
  explicit basic_entry_wrapper(T& entry)
      : entry_(&entry) {}

  size_t size() const { return entry_->size(); }
  std::string path() const { return entry_->path(); }
  std::string name() const { return entry_->name(); }
  std::string type() const { return entry_->type_string(); }

  uint16_t permissions() const { return entry_->get_permissions(); }
  void set_permissions(uint16_t perm) { entry_->set_permissions(perm); }
  uint16_t uid() const { return entry_->get_uid(); }
  void set_uid(uint16_t uid) { entry_->set_uid(uid); }
  uint16_t gid() const { return entry_->get_gid(); }
  void set_gid(uint16_t gid) { entry_->set_gid(gid); }
  uint64_t atime() const { return entry_->get_atime(); }
  void set_atime(uint64_t atime) { entry_->set_atime(atime); }
  uint64_t mtime() const { return entry_->get_mtime(); }
  void set_mtime(uint64_t mtime) { entry_->set_mtime(mtime); }
  uint64_t ctime() const { return entry_->get_ctime(); }
  void set_ctime(uint64_t ctime) { entry_->set_ctime(ctime); }

 private:
  T* entry_;
};

using entry_wrapper = basic_entry_wrapper<entry_interface const>;
using mutable_entry_wrapper = basic_entry_wrapper<entry_interface>;

class inode_wrapper {
 public:
  explicit inode_wrapper(inode const* ino)
      : ino_(ino) {}

  size_t similarity_hash() const { return ino_->similarity_hash(); }
  size_t refcount() const { return ino_->files().size(); }
  py::list paths() const {
    py::list ps;
    auto& fs = ino_->files();
    for (auto& f : fs) {
      ps.append(f->path());
    }
    return ps;
  }
  size_t size() const { return ino_->size(); }
  inode const* get() const { return ino_; }

 private:
  inode const* ino_;
};

} // namespace

class python_script::impl {
 public:
  impl(logger& lgr, const std::string& code, const std::string& ctor);
  ~impl();

  void configure(options_interface const& oi);
  bool filter(entry_interface const& ei);
  void transform(entry_interface& ei);
  void order(inode_vector& iv);

  bool has_configure() const { return has_configure_; }
  bool has_filter() const { return has_filter_; }
  bool has_transform() const { return has_transform_; }
  bool has_order() const { return has_order_; }

 private:
  void check_instance_methods(py::object obj) const;
  void log_py_error() const;

  using log_proxy_t = log_proxy<debug_logger_policy>;
  using clock = std::chrono::steady_clock;

  class timer {
   public:
    explicit timer(clock::duration& d)
        : start_(clock::now())
        , d_(d) {}

    ~timer() { d_ += clock::now() - start_; }

   private:
    clock::time_point start_;
    clock::duration& d_;
  };

  log_proxy_t log_;
  py_logger pylog_;
  bool has_configure_{false};
  bool has_filter_{false};
  bool has_transform_{false};
  bool has_order_{false};
  py::object instance_;
  py::object main_module_;
  py::object main_namespace_;
  clock::duration configure_time_{};
  clock::duration filter_time_{};
  clock::duration transform_time_{};
  clock::duration order_time_{};
};

python_script::impl::impl(logger& lgr, const std::string& code,
                          const std::string& ctor)
    : log_(lgr)
    , pylog_(lgr) {
  try {
    init_python();

    main_module_ = py::import("__main__");
    main_namespace_ = main_module_.attr("__dict__");

    py::scope scope(main_module_);

    main_namespace_["dwarfs_logger"] =
        py::class_<py_logger, boost::noncopyable>("dwarfs_logger", py::no_init)
            .def("error", &py_logger::error)
            .def("warn", &py_logger::warn)
            .def("info", &py_logger::info)
            .def("debug", &py_logger::debug)
            .def("trace", &py_logger::trace);

    main_namespace_["file_order_mode"] =
        py::enum_<file_order_mode>("file_order_mode")
            .value("none", file_order_mode::NONE)
            .value("path", file_order_mode::PATH)
            .value("script", file_order_mode::SCRIPT)
            .value("similarity", file_order_mode::SIMILARITY)
            .value("nilsimsa", file_order_mode::NILSIMSA);

    main_namespace_["set_mode"] =
        py::enum_<options_interface::set_mode>("set_mode")
            .value("default", options_interface::DEFAULT)
            .value("override", options_interface::OVERRIDE);

    main_namespace_["dwarfs_options"] =
        py::class_<options_interface, boost::noncopyable>("dwarfs_options",
                                                          py::no_init)
            .def("enable_similarity", &options_interface::enable_similarity)
            .def("set_order", &options_interface::set_order)
            .def("set_remove_empty_dirs",
                 &options_interface::set_remove_empty_dirs);

    main_namespace_["inode_wrapper"] =
        py::class_<inode_wrapper, std::shared_ptr<inode_wrapper>>(
            "inode_wrapper", py::no_init)
            .def("similarity_hash", &inode_wrapper::similarity_hash)
            .def("refcount", &inode_wrapper::refcount)
            .def("paths", &inode_wrapper::paths)
            .def("size", &inode_wrapper::size);

    main_namespace_["entry_wrapper"] =
        py::class_<entry_wrapper, std::shared_ptr<entry_wrapper>>(
            "entry_wrapper", py::no_init)
            .def("name", &entry_wrapper::name)
            .def("type", &entry_wrapper::type)
            .def("path", &entry_wrapper::path)
            .def("size", &entry_wrapper::size)
            .def("permissions", &entry_wrapper::permissions)
            .def("uid", &entry_wrapper::uid)
            .def("gid", &entry_wrapper::gid)
            .def("atime", &entry_wrapper::atime)
            .def("mtime", &entry_wrapper::mtime)
            .def("ctime", &entry_wrapper::ctime);

    main_namespace_["mutable_entry_wrapper"] =
        py::class_<mutable_entry_wrapper,
                   std::shared_ptr<mutable_entry_wrapper>>(
            "mutable_entry_wrapper", py::no_init)
            .def("name", &mutable_entry_wrapper::name)
            .def("type", &mutable_entry_wrapper::type)
            .def("path", &mutable_entry_wrapper::path)
            .def("size", &mutable_entry_wrapper::size)
            .def("permissions", &mutable_entry_wrapper::permissions)
            .def("uid", &mutable_entry_wrapper::uid)
            .def("gid", &mutable_entry_wrapper::gid)
            .def("atime", &mutable_entry_wrapper::atime)
            .def("mtime", &mutable_entry_wrapper::mtime)
            .def("ctime", &mutable_entry_wrapper::ctime)
            .def("set_permissions", &mutable_entry_wrapper::set_permissions)
            .def("set_uid", &mutable_entry_wrapper::set_uid)
            .def("set_gid", &mutable_entry_wrapper::set_gid)
            .def("set_atime", &mutable_entry_wrapper::set_atime)
            .def("set_mtime", &mutable_entry_wrapper::set_mtime)
            .def("set_ctime", &mutable_entry_wrapper::set_ctime);

    main_namespace_["logger"] = py::ptr(&pylog_);

    py::exec(code.c_str(), main_namespace_);

    instance_ = py::eval(ctor.c_str(), main_namespace_);

    check_instance_methods(instance_);

    has_configure_ = has_callable(instance_, "configure");
    has_filter_ = has_callable(instance_, "filter");
    has_transform_ = has_callable(instance_, "transform");
    has_order_ = has_callable(instance_, "order");
  } catch (py::error_already_set const&) {
    log_py_error();
    DWARFS_THROW(runtime_error, "error initializing script");
  }
}

void python_script::impl::log_py_error() const {
  PyObject *exc, *val, *tb;
  PyErr_Fetch(&exc, &val, &tb);
  PyErr_NormalizeException(&exc, &val, &tb);

  py::handle<> hexc(exc), hval(py::allow_null(val)), htb(py::allow_null(tb));

  if (!hval) {
    LOG_ERROR << std::string(py::extract<std::string>(py::str(hexc)));
  } else {
    py::object traceback(py::import("traceback"));
    py::object format_exception(traceback.attr("format_exception"));
    py::list formatted_list(format_exception(hexc, hval, htb));
    for (int count = 0; count < len(formatted_list); ++count) {
      LOG_ERROR << std::string(
          py::extract<std::string>(formatted_list[count].slice(0, -1)));
    }
  }
}

void python_script::impl::check_instance_methods(py::object obj) const {
  for (py::stl_input_iterator<py::str>
           it(py::object(py::handle<>(PyObject_Dir(obj.ptr())))),
       end;
       it != end; ++it) {
    if (!it->startswith("_") && callable(obj.attr(*it))) {
      std::string name{py::extract<char const*>(*it)};
      if (supported_methods.find(name) == supported_methods.end()) {
        LOG_WARN << "unknown method '" << name << "' found in Python class";
      }
    }
  }
}

python_script::impl::~impl() {
  std::vector<std::string> timings;
  auto add_timing = [&](bool flag, std::string_view name, auto const& d) {
    using floatsec = std::chrono::duration<float>;
    if (flag) {
      timings.push_back(
          fmt::format("{0} {1:.2f}s", name,
                      std::chrono::duration_cast<floatsec>(d).count()));
    }
  };

  add_timing(has_configure_, "configure", configure_time_);
  add_timing(has_filter_, "filter", filter_time_);
  add_timing(has_transform_, "transform", transform_time_);
  add_timing(has_order_, "order", order_time_);

  LOG_INFO << "script time: " << boost::join(timings, ", ");

  // nothing else, really, as boost::python docs forbid using Py_Finalize
}

void python_script::impl::configure(options_interface const& oi) {
  timer tmr(configure_time_);
  try {
    instance_.attr("configure")(py::ptr(&oi));
  } catch (py::error_already_set const&) {
    log_py_error();
    DWARFS_THROW(runtime_error, "error in configure");
  }
}

bool python_script::impl::filter(entry_interface const& ei) {
  timer tmr(filter_time_);
  try {
    return py::extract<bool>(
        instance_.attr("filter")(std::make_shared<entry_wrapper>(ei)));
  } catch (py::error_already_set const&) {
    log_py_error();
    DWARFS_THROW(runtime_error, "error filtering entry");
  }
}

void python_script::impl::transform(entry_interface& ei) {
  timer tmr(transform_time_);
  try {
    instance_.attr("transform")(std::make_shared<mutable_entry_wrapper>(ei));
  } catch (py::error_already_set const&) {
    log_py_error();
    DWARFS_THROW(runtime_error, "error transforming entry");
  }
}

void python_script::impl::order(inode_vector& iv) {
  timer tmr(order_time_);
  try {
    py::list files;

    {
      auto td = LOG_TIMED_DEBUG;

      for (size_t i = 0; i < iv.size(); ++i) {
        files.append(std::make_shared<inode_wrapper>(iv[i].get()));
      }

      td << "prepared files for ordering";
    }

    py::object ordered;

    {
      auto td = LOG_TIMED_DEBUG;
      ordered = instance_.attr("order")(files);
      td << "ordered files in script code";
    }

    folly::F14FastMap<inode const*, size_t> priority(iv.size());

    auto td = LOG_TIMED_DEBUG;
    size_t index = 0;

    for (py::stl_input_iterator<py::object> it(ordered), end; it != end; ++it) {
      auto wrapper{py::extract<std::shared_ptr<inode_wrapper>>(*it)()};
      priority[wrapper->get()] = index++;
    }

    if (index != iv.size()) {
      DWARFS_THROW(runtime_error, "order() returned different number of files");
    }

    std::sort(iv.begin(), iv.end(),
              [&](inode_ptr const& a, inode_ptr const& b) {
                auto ia = priority.find(a.get());
                auto ib = priority.find(b.get());
                if (ia == priority.end() || ib == priority.end()) {
                  DWARFS_THROW(runtime_error,
                               "invalid inode pointer while ordering files");
                }
                return ia->second < ib->second;
              });

    td << "applied new inode order";
  } catch (py::error_already_set const&) {
    log_py_error();
    DWARFS_THROW(runtime_error, "error ordering inodes");
  }
}

python_script::python_script(logger& lgr, const std::string& code,
                             const std::string& ctor)
    : impl_(std::make_unique<impl>(lgr, code, ctor)) {}

python_script::~python_script() = default;

bool python_script::has_configure() const { return impl_->has_configure(); }
bool python_script::has_filter() const { return impl_->has_filter(); }
bool python_script::has_transform() const { return impl_->has_transform(); }
bool python_script::has_order() const { return impl_->has_order(); }

void python_script::configure(options_interface const& oi) {
  impl_->configure(oi);
}

bool python_script::filter(entry_interface const& ei) {
  return impl_->filter(ei);
}

void python_script::transform(entry_interface& ei) { impl_->transform(ei); }

void python_script::order(inode_vector& iv) { impl_->order(iv); }

} // namespace dwarfs
