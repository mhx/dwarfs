IMAGE_UBUNTU="dwarfs-buildenv"
IMAGE_DEBIAN="dwarfs-buildenv-debian"
IMAGE_ALPINE="dwarfs-buildenv-alpine"
IMAGE_ALPINE_RISCV="dwarfs-buildenv-alpine-riscv64"
IMAGE_UBUNTU2204="dwarfs-buildenv-ubuntu2204"
IMAGE_FEDORA="dwarfs-buildenv-fedora"
IMAGE_ARCH="dwarfs-buildenv-arch"

ROOT_DIR:=$(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))
NPROC:=$(shell nproc)

COMMON_RUN_OPTS=-it --rm \
		--cap-add SYS_ADMIN --device /dev/fuse --privileged \
		--mount type=bind,source="$(ROOT_DIR)"/..,target=/workspace,readonly \
		--mount type=bind,source="$(ROOT_DIR)"/../@docker-ccache,target=/ccache \
		--mount type=bind,source="$(ROOT_DIR)"/../@docker-home,target=/home/mhx \
		--mount type=bind,source="$(ROOT_DIR)"/../@docker-build,target=/home/mhx/build \
		--env CCACHE_DIR=/ccache --env CTEST_PARALLEL_LEVEL=$(NPROC) \
		--entrypoint /bin/bash

build:
	docker build -t $(IMAGE_UBUNTU) .

run:
	@docker run $(COMMON_RUN_OPTS) $(IMAGE_UBUNTU)

build_arm:
	docker build -f Dockerfile.ubuntu -t $(IMAGE_UBUNTU) --build-arg ARCH=arm64v8 .

build_debian:
	docker build -f Dockerfile.debian -t $(IMAGE_DEBIAN) .

run_debian:
	@docker run $(COMMON_RUN_OPTS) $(IMAGE_DEBIAN)

build_alpine:
	docker build -f Dockerfile.alpine -t $(IMAGE_ALPINE) .

run_alpine:
	@docker run $(COMMON_RUN_OPTS) $(IMAGE_ALPINE)

build_alpine_riscv:
	docker build -f Dockerfile.alpine -t $(IMAGE_ALPINE_RISCV) --build-arg ARCH=riscv64 .

run_alpine_riscv:
	@docker run $(COMMON_RUN_OPTS) $(IMAGE_ALPINE_RISCV)

build_ubuntu2204:
	docker build -f Dockerfile.ubuntu-2204 -t $(IMAGE_UBUNTU2204) .

run_ubuntu2204:
	@docker run $(COMMON_RUN_OPTS) $(IMAGE_UBUNTU2204)

build_fedora:
	docker build -f Dockerfile.fedora -t $(IMAGE_FEDORA) .

run_fedora:
	@docker run $(COMMON_RUN_OPTS) $(IMAGE_FEDORA)

build_arch:
	docker build -f Dockerfile.arch -t $(IMAGE_ARCH) .

run_arch:
	@docker run $(COMMON_RUN_OPTS) $(IMAGE_ARCH)
