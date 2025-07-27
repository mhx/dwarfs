IMAGE_UBUNTU="dwarfs-buildenv"
IMAGE_DEBIAN="dwarfs-buildenv-debian"
IMAGE_ALPINE="dwarfs-buildenv-alpine"
IMAGE_ALPINE_NEWARCH="dwarfs-buildenv-alpine-newarch"
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
	docker buildx build -f Dockerfile.alpine --build-arg TARGET_ARCHS=x86_64,aarch64,riscv64,ppc64le,i386 -t $(IMAGE_ALPINE) .

run_alpine:
	@docker run $(COMMON_RUN_OPTS) $(IMAGE_ALPINE)

build_alpine_newarch:
	docker buildx build -f Dockerfile.alpine --build-arg TARGET_ARCHS=s390x,arm -t $(IMAGE_ALPINE_NEWARCH) .

run_alpine_newarch:
	@docker run $(COMMON_RUN_OPTS) $(IMAGE_ALPINE_NEWARCH)

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
