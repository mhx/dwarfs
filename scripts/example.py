class mkdwarfs(object):
    def __init__(self):
        logger.info("this is python!")

    def configure(self, config):
        config.enable_similarity()
        config.set_order(file_order_mode.script, set_mode.override)
        config.set_remove_empty_dirs(True, set_mode.default)

    def filter(self, entry):
        logger.debug(f"filter: {entry.path()} [{entry.type()}]")
        if entry.type() == 'directory' and entry.name() == 'dev':
            return False
        return True

    def transform(self, entry):
        logger.debug(f"transform {entry.path()}")
        entry.set_permissions(entry.permissions() & 0o7555)
        return entry

    def order(self, inodes):
        logger.info("order")
        for i in inodes:
            logger.debug(f"inode: {i.similarity_hash()} {i.size()} {i.refcount()}")
            for p in i.paths():
                logger.debug(f"  file: {p}")
        return reversed(inodes)

    def _something_private(self):
        pass
