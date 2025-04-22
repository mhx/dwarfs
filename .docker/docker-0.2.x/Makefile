IMAGE="dwarfs-alpine-old"

ROOT_DIR:=$(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))
NPROC:=$(shell nproc)

COMMON_RUN_OPTS=-it --rm \
		--cap-add SYS_ADMIN --device /dev/fuse --privileged \
		--mount type=bind,source=/home/mhx/git/github/dwarfs,target=/workspace,readonly \
		--mount type=bind,source="$(ROOT_DIR)"/../@ccache,target=/ccache \
		--mount type=bind,source="$(ROOT_DIR)"/../@home,target=/root \
		--env CCACHE_DIR=/ccache --env CTEST_PARALLEL_LEVEL=$(NPROC) \
		--entrypoint /bin/bash

build:
	docker build -t $(IMAGE) .

run:
	@docker run $(COMMON_RUN_OPTS) $(IMAGE)
