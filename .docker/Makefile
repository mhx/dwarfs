IMAGE_UBUNTU="dwarfs-buildenv"
IMAGE_DEBIAN="dwarfs-buildenv-debian"
IMAGE_ALPINE="dwarfs-buildenv-alpine"
IMAGE_ALPINE_NEWARCH="dwarfs-buildenv-alpine-newarch"
IMAGE_ALPINE_APK="dwarfs-alpine-apk"
IMAGE_UBUNTU2204="dwarfs-buildenv-ubuntu2204"
IMAGE_FEDORA="dwarfs-buildenv-fedora"
IMAGE_SUSE="dwarfs-buildenv-suse"
IMAGE_ARCH="dwarfs-buildenv-arch"

ROOT_DIR:=$(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))
NPROC:=$(shell nproc)

ALPINE_DOCKERFILE:=Dockerfile.alpine_$(shell uname -m)

COMMON_RUN_OPTS=-it --rm \
		--cap-add SYS_ADMIN --device /dev/fuse \
		--security-opt seccomp:unconfined --security-opt apparmor:unconfined \
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

Dockerfile.alpine_x86_64: build_dockerfile.pl
	perl build_dockerfile.pl --targets=x86_64,aarch64,loongarch64,riscv64,ppc64,ppc64le,s390x,arm,i386 --upx=amd64 --with-bloaty > $@

Dockerfile.alpine_aarch64: build_dockerfile.pl
	perl build_dockerfile.pl --targets=aarch64 --upx=arm64 > $@

dockerfiles: Dockerfile.alpine_x86_64 Dockerfile.alpine_aarch64

build_alpine: $(ALPINE_DOCKERFILE)
	docker buildx build -f $(ALPINE_DOCKERFILE) -t $(IMAGE_ALPINE) .

run_alpine:
	@docker run $(COMMON_RUN_OPTS) $(IMAGE_ALPINE)

build_alpine_apk:
	docker buildx build -f Dockerfile.alpine-apk -t $(IMAGE_ALPINE_APK) .

run_alpine_apk:
	@docker run -it --rm \
		--mount type=bind,source="$(ROOT_DIR)"/../@docker-apk-home,target=/home/mhx \
		$(IMAGE_ALPINE_APK)

build_ubuntu2204:
	docker build -f Dockerfile.ubuntu-2204 -t $(IMAGE_UBUNTU2204) .

run_ubuntu2204:
	@docker run $(COMMON_RUN_OPTS) $(IMAGE_UBUNTU2204)

build_fedora:
	docker build -f Dockerfile.fedora -t $(IMAGE_FEDORA) .

run_fedora:
	@docker run $(COMMON_RUN_OPTS) $(IMAGE_FEDORA)

build_suse:
	docker build -f Dockerfile.suse -t $(IMAGE_SUSE) .

run_suse:
	@docker run $(COMMON_RUN_OPTS) $(IMAGE_SUSE)

build_arch:
	docker build -f Dockerfile.arch -t $(IMAGE_ARCH) .

run_arch:
	@docker run $(COMMON_RUN_OPTS) $(IMAGE_ARCH)

export_cache:
	@docker buildx build -f Dockerfile.exportcache -t export-cache .

import_cache:
	@docker buildx build -f Dockerfile.importcache -t import-cache .
