class mkdwarfs(object):
    """
    The class defining mkdwarfs customization.

    If this is named `mkdwarfs`, you only have to specify the path to
    the script file with `--script`. You can define multiple classes in
    a single script, in which case you'll have to pass the class name
    in addition to the script path as `--script <file>:<class>`. If the
    class has a custom contructor, it is also possible to pass arguments
    to the constuctor from the command line.

    All methods are optional. If you want to define methods beyond the
    ones specified below, make sure you start their names with an
    underscore, otherwise there will be a warning to ensure you don't
    accidentally mistype the names of the methods.

    You can use the global `logger` object for logging.
    """
    def __init__(self):
        """
        Optional constructor
        """
        logger.info("this is python!")

    def configure(self, config):
        """
        Configuration

        This will be called early and allows you to change the default
        for or even override (command line) parameters. Only a small
        number of parameters are currently supported.
        """
        # Enable similarity hash computation, useful if you actually
        # want to use it in the `order` method.
        config.enable_similarity()
        config.set_order(file_order_mode.script, set_mode.override)
        config.set_remove_empty_dirs(True, set_mode.default)

    def filter(self, entry):
        """
        Filtering

        This will be called for every file system entry. If you return
        `False`, the entry will be skipped.
        """
        logger.debug(f"filter: {entry.path()} [{entry.type()}]")
        if entry.type() == 'directory' and entry.name() == 'dev':
            return False
        return True

    def transform(self, entry):
        """
        Transformation

        This will be called for every entry that has not been filtered,
        and allows you to change certain attributes, such as permissions,
        ownership, or timestamps.
        """
        logger.debug(f"transform {entry.path()}")
        entry.set_permissions(entry.permissions() & 0o7555)
        return entry

    def order(self, inodes):
        """
        Inode Ordering

        This will be called for every regular file inode, after all
        entries have been scanned and files have been deduplicated.
        """
        logger.info("order")
        for i in inodes:
            logger.debug(f"inode: {i.similarity_hash()} {i.size()} {i.refcount()}")
            for p in i.paths():
                logger.debug(f"  file: {p}")
        return reversed(inodes)

    def _something_private(self):
        pass
