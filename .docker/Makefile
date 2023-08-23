IMAGE="dwarfs-buildenv"

ROOT_DIR:=$(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))

build:
	docker build -t $(IMAGE) .

run:
	@docker run -it --rm \
            --cap-add SYS_ADMIN --device /dev/fuse --privileged \
            --mount type=bind,source="$(ROOT_DIR)"/..,target=/workspace,readonly \
            --mount type=bind,source="$(ROOT_DIR)"/../@docker-ccache,target=/ccache \
            --mount type=bind,source="$(ROOT_DIR)"/../@docker-build,target=/home/ubuntu/build \
            --env CCACHE_DIR=/ccache --env CTEST_PARALLEL_LEVEL=$(nproc) \
            --entrypoint /bin/bash $(IMAGE)
