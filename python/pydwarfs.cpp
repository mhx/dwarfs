#include <fmt/format.h>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include <dwarfs/logger.h>
#include <dwarfs/os_access_generic.h>
#include <dwarfs/reader/filesystem_v2.h>
#include <dwarfs/reader/fsinfo_options.h>

namespace dr = dwarfs::reader;
namespace fs = std::filesystem;
namespace py = pybind11;

namespace {

class py_logger : public dwarfs::logger {
 public:
  py_logger(level_type threshold = level_type::INFO)
      : threshold_{threshold} {
    if (threshold >= level_type::DEBUG) {
      set_policy<dwarfs::debug_logger_policy>();
    } else {
      set_policy<dwarfs::prod_logger_policy>();
    }
  }

  void write(level_type level, std::string const& msg, char const* file,
             int line) override {
    if (level <= threshold_ || level == FATAL) {
      PYBIND11_OVERRIDE_PURE(
          void,           // Return type
          dwarfs::logger, // Parent class
          write,          // Name of function in C++ (must match Python name)
          level, msg, file, line // Argument(s)
      );
    }
  }

 private:
  level_type threshold_;
};

class py_filesystem : public dr::filesystem_v2 {
 public:
  py_filesystem(dwarfs::logger& logger, dwarfs::os_access const& os_access,
                fs::path const& root)
      : dr::filesystem_v2(logger, os_access, root)
      , logger_{py::cast(logger)}
      , os_access_{py::cast(os_access)} {}

 private:
  py::object logger_;
  py::object os_access_;
};

} // namespace

PYBIND11_MODULE(pydwarfs, m) {
  py::class_<dwarfs::logger, py_logger> logger(m, "logger");
  logger.def(py::init<dwarfs::logger::level_type>())
      .def("write", &dwarfs::logger::write);

  py::enum_<dwarfs::logger::level_type>(logger, "level_type")
      .value("FATAL", dwarfs::logger::level_type::FATAL)
      .value("ERROR", dwarfs::logger::level_type::ERROR)
      .value("WARN", dwarfs::logger::level_type::WARN)
      .value("INFO", dwarfs::logger::level_type::INFO)
      .value("VERBOSE", dwarfs::logger::level_type::VERBOSE)
      .value("DEBUG", dwarfs::logger::level_type::DEBUG)
      .value("TRACE", dwarfs::logger::level_type::TRACE)
      .export_values();

  py::class_<dwarfs::stream_logger, dwarfs::logger>(m, "stream_logger")
      .def(py::init<>());

  py::class_<dwarfs::os_access>(m, "os_access");

  py::class_<dwarfs::os_access_generic, dwarfs::os_access>(m,
                                                           "os_access_generic")
      .def(py::init<>());

  auto mr = m.def_submodule("reader");

  py::enum_<dr::fsinfo_feature>(mr, "fsinfo_feature")
      .value("version", dr::fsinfo_feature::version)
      .value("history", dr::fsinfo_feature::history)
      .value("metadata_summary", dr::fsinfo_feature::metadata_summary)
      .value("metadata_details", dr::fsinfo_feature::metadata_details)
      .value("metadata_full_dump", dr::fsinfo_feature::metadata_full_dump)
      .value("frozen_analysis", dr::fsinfo_feature::frozen_analysis)
      .value("frozen_layout", dr::fsinfo_feature::frozen_layout)
      .value("directory_tree", dr::fsinfo_feature::directory_tree)
      .value("section_details", dr::fsinfo_feature::section_details)
      .value("chunk_details", dr::fsinfo_feature::chunk_details)
      .export_values();

  py::class_<dr::fsinfo_features>(mr, "fsinfo_features")
      .def(py::init<>())
      .def(py::init<std::initializer_list<dr::fsinfo_feature>>())
      .def("for_level", &dr::fsinfo_features::for_level)
      .def("__repr__", &dr::fsinfo_features::to_string);

  py::enum_<dr::block_access_level>(mr, "block_access_level")
      .value("no_access", dr::block_access_level::no_access)
      .value("no_verify", dr::block_access_level::no_verify)
      .value("unrestricted", dr::block_access_level::unrestricted)
      .export_values();

  py::class_<dr::fsinfo_options>(mr, "fsinfo_options")
      .def(py::init<>())
      .def_readwrite("features", &dr::fsinfo_options::features)
      .def_readwrite("block_access", &dr::fsinfo_options::block_access);

  py::class_<dr::inode_view>(mr, "inode_view")
      .def(py::init<>())
      // .def("mode", &dr::inode_view::mode)
      .def("mode_string", &dr::inode_view::mode_string)
      .def("perm_string", &dr::inode_view::perm_string)
      // .def("type", &dr::inode_view::type)
      .def("is_regular_file", &dr::inode_view::is_regular_file)
      .def("is_directory", &dr::inode_view::is_directory)
      .def("is_symlink", &dr::inode_view::is_symlink)
      .def("getuid", &dr::inode_view::getuid)
      .def("getgid", &dr::inode_view::getgid)
      .def("inode_num", &dr::inode_view::inode_num)
      .def("__repr__", [](dr::inode_view const& iv) {
        return fmt::format("inode_view(inode={})", iv.inode_num());
      });

  py::class_<py_filesystem>(mr, "filesystem")
      .def(py::init<dwarfs::logger&, dwarfs::os_access const&,
                    fs::path const&>())
      .def("dump", py::overload_cast<dr::fsinfo_options const&>(
                       &dr::filesystem_v2::dump, py::const_))
      .def("find",
           py::overload_cast<char const*>(&dr::filesystem_v2::find, py::const_))
      .def("open", py::overload_cast<dr::inode_view>(&dr::filesystem_v2::open,
                                                     py::const_))
      .def("read", py::overload_cast<uint32_t>(&dr::filesystem_v2::read_string,
                                               py::const_), py::call_guard<py::gil_scoped_release>());
}
