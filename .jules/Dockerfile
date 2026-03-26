FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    sudo \
    curl \
    qt6-base-dev \
    qt6-svg-dev \
    qt6-tools-dev \
    qt6-tools-dev-tools \
    libkf5wallet-dev \
    libkf6notifications-dev \
    libkf6coreaddons-dev \
    libkf6xmlgui-dev \
    libkf6configwidgets-dev \
    libkf6i18n-dev \
    libgl1-mesa-dev \
    libsqlcipher-dev \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

RUN useradd -m jules && echo "jules ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers
USER jules
WORKDIR /workspace
