IMAGE="dwarfs-buildenv"
IMAGE_FEDORA="dwarfs-buildenv-fedora"

ROOT_DIR:=$(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))
NPROC:=$(shell nproc)

build:
	docker build -t $(IMAGE) .

build_arm:
	docker build -t $(IMAGE) --build-arg ARCH=arm64v8 .

run:
	@docker run -it --rm \
            --cap-add SYS_ADMIN --device /dev/fuse --privileged \
            --mount type=bind,source="$(ROOT_DIR)"/..,target=/workspace,readonly \
            --mount type=bind,source="$(ROOT_DIR)"/../@docker-ccache,target=/ccache \
            --mount type=bind,source="$(ROOT_DIR)"/../@docker-home,target=/home/ubuntu \
            --mount type=bind,source="$(ROOT_DIR)"/../@docker-build,target=/home/ubuntu/build \
            --env CCACHE_DIR=/ccache --env CTEST_PARALLEL_LEVEL=$(NPROC) \
            --entrypoint /bin/bash $(IMAGE)

build_fedora:
	docker build -f Dockerfile.fedora -t $(IMAGE_FEDORA) .

run_fedora:
	@docker run -it --rm \
            --cap-add SYS_ADMIN --device /dev/fuse --privileged \
            --mount type=bind,source="$(ROOT_DIR)"/..,target=/workspace,readonly \
            --mount type=bind,source="$(ROOT_DIR)"/../@docker-ccache,target=/ccache \
            --mount type=bind,source="$(ROOT_DIR)"/../@docker-home,target=/home/mhx \
            --mount type=bind,source="$(ROOT_DIR)"/../@docker-build,target=/home/mhx/build \
            --env CCACHE_DIR=/ccache --env CTEST_PARALLEL_LEVEL=$(NPROC) \
            --entrypoint /bin/bash $(IMAGE_FEDORA)
