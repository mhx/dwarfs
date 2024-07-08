#include <iostream>

#include <thrift/lib/cpp2/frozen/FrozenUtil.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include <dwarfs/fstypes.h>
#include <dwarfs/logger.h>
#include <dwarfs/mmap.h>
#include <dwarfs/metadata_freezer.h>
#include <dwarfs/metadata_v2.h>
#include <dwarfs/options.h>
#include <dwarfs/terminal.h>

#include <dwarfs/gen-cpp2/metadata_layouts.h>

int main(int argc, char** argv) {
  apache::thrift::BinarySerializer serializer;
  std::shared_ptr<dwarfs::terminal const> term = dwarfs::terminal::create();
  dwarfs::stream_logger lgr(term, std::cerr);

  auto mm = dwarfs::mmap("/home/mhx/wikipedia-metadata.binary");

  dwarfs::thrift::metadata::metadata md;
  serializer.deserialize(folly::ByteRange(mm.as<uint8_t>(), mm.size()), md);

  std::string s;
  apache::thrift::frozen::freezeToStringMalloc(md, s);

  std::cout << s.size() << std::endl;

  auto[schema, data] = dwarfs::metadata_freezer::freeze(md);

  std::cout << "schema size: " << schema.size() << ", data size: " << data.size() << std::endl;

  dwarfs::metadata_v2 md2(lgr, schema, data, {});

  dwarfs::filesystem_info fi;

  md2.dump(std::cout, 2, fi, [](auto const&, uint32_t) {});

  return 0;
}
