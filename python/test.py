import logging
import pydwarfs as pd

class python_logger(pd.logger):
    level_map = {
        pd.logger.TRACE: logging.DEBUG,
        pd.logger.DEBUG: logging.DEBUG,
        pd.logger.VERBOSE: logging.INFO,
        pd.logger.INFO: logging.INFO,
        pd.logger.WARN: logging.WARNING,
        pd.logger.ERROR: logging.ERROR,
        pd.logger.FATAL: logging.CRITICAL,
    }

    def __init__(self, level = pd.logger.INFO):
        self._logger = logging.getLogger("pydwarfs")
        super().__init__(level)

    def write(self, level, msg, file, line):
        rec = self._logger.makeRecord("pydwarfs", self.level_map[level], file, line, msg, None, None)
        self._logger.handle(rec)

logging.basicConfig(format='%(asctime)s,%(msecs)03d %(levelname)-8s [%(filename)s:%(lineno)d] %(message)s',
    datefmt='%Y-%m-%d:%H:%M:%S',
    level=logging.DEBUG)

lgr = python_logger(pd.logger.VERBOSE)
os = pd.os_access_generic()
fs = pd.reader.filesystem(lgr, os, "/home/mhx/perl-install-l6.dwarfs")
# fiopts = pd.reader.fsinfo_options()
# fiopts.features = pd.reader.fsinfo_features.for_level(2)
# fiopts.block_access = pd.reader.block_access_level.unrestricted
# print(fs.dump(fiopts))
iv = fs.find("/default/perl-5.33.3/lib/5.33.3/pod/perltodo.pod")
print(iv)
fd = fs.open(iv)
data = fs.read(fd)
print(data)

iv = fs.find("/default/perl-5.33.3/lib/5.33.3/pod")
print(iv)
try:
    fd = fs.open(iv)
except RuntimeError as e:
    print(e)
