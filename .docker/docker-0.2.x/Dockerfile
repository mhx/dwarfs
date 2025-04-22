# syntax=docker/dockerfile:1
ARG ARCH=amd64
FROM $ARCH/alpine:3.14
RUN apk update
RUN apk upgrade
RUN apk add --no-cache \
        bash-completion \
        build-base \
        wget \
        curl \
        vim \
        tar \
        ripgrep \
        py3-pip \
        less \
        gcc \
        g++ \
        git \
        ccache \
        samurai \
        cmake \
        make \
        bison \
        flex \
        ronn \
        fuse \
        fuse-dev \
        pkgconf \
        binutils-dev \
        libevent-dev \
        libevent-static \
        linux-headers \
        date-dev \
        range-v3-dev \
        zlib-static \
        libucontext-dev \
        libdwarf-dev \
        elfutils-dev \
        nlohmann-json \
        meson \
        autoconf \
        strace \
        gdb \
        zstd-dev \
        zstd-static \
        lz4-dev \
        lz4-static \
        eudev-dev \
        gtest-dev \
        xz \
        xz-dev \
        boost-dev \
        boost-static \
        sparsehash \
        icu-static \
        openssl-libs-static \
        openssl-dev

COPY double-conversion-3.1.5.tar.gz /opt/
COPY fmt-7.1.3.tar.gz /opt/
COPY fuse-3.10.4.tar.xz /opt/
COPY glog-0.5.0.tar.gz /opt/
COPY build_libs.sh /opt/
RUN bash /opt/build_libs.sh

ENTRYPOINT /bin/bash
