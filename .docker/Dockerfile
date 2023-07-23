# syntax=docker/dockerfile:1
ARG ARCH=amd64
FROM $ARCH/ubuntu:22.04
RUN apt update && apt install -y gnupg wget software-properties-common
RUN wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - | tee /etc/apt/trusted.gpg.d/kitware.gpg >/dev/null
RUN apt-add-repository 'deb https://apt.kitware.com/ubuntu/ jammy main'
RUN apt install -y \
        gcc \
        g++ \
        gcc-12 \
        g++-12 \
        clang \
        clang-15 \
        git \
        vim \
        zstd \
        ccache \
        ninja-build \
        cmake \
        make \
        bison \
        flex \
        ronn \
        fuse3 \
        pkg-config \
        binutils-dev \
        libacl1-dev \
        libarchive-dev \
        libbenchmark-dev \
        libboost-chrono-dev \
        libboost-context-dev \
        libboost-filesystem-dev \
        libboost-iostreams-dev \
        libboost-program-options-dev \
        libboost-regex-dev \
        libboost-system-dev \
        libboost-thread-dev \
        libbrotli-dev \
        libevent-dev \
        libjemalloc-dev \
        libdouble-conversion-dev \
        libiberty-dev \
        liblz4-dev \
        liblzma-dev \
        libmagic-dev \
        libssl-dev \
        libunwind-dev \
        libdwarf-dev \
        libelf-dev \
        libfmt-dev \
        libfuse-dev \
        libfuse3-dev \
        libgoogle-glog-dev \
        libutfcpp-dev \
        libflac++-dev \
        bash-completion \
        upx
COPY install-static-libs.sh /usr/local/bin/install-static-libs.sh
RUN bash /usr/local/bin/install-static-libs.sh
RUN useradd -g users -u 1000 -m mhx
ARG SCRIPT=build-linux.sh
COPY $SCRIPT /usr/local/bin/run.sh
RUN chmod +x /usr/local/bin/run.sh
USER mhx
ENTRYPOINT /usr/local/bin/run.sh
