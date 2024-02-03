# Fetch ubuntu image
FROM ubuntu:latest AS base
# Attempt to auto-detect the best mirror (desn't always work well though)
ARG APT_MIRROR="mirror://mirrors.ubuntu.com/mirrors.txt"
# Use this one if you are in Australia
# ARG APT_MIRROR="http://mirror.aarnet.edu.au/pub/ubuntu/archive/"

# Configure mirror. Pass --build-arg APT_MIRROR=<mirror URL> to set a mirror if this is slow
RUN sed -i "s#htt[p|ps]://archive.ubuntu.com/ubuntu/#$APT_MIRROR#g" /etc/apt/sources.list

# Install packages
RUN \
    apt update && \
    apt install -y git python3 && \
    apt install -y cmake gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential

# Install Pico SDK into a new stage
FROM base as buildtools

RUN \
    mkdir -p /pico/ && \
    cd /pico/ && \
    git clone https://github.com/raspberrypi/pico-sdk.git --branch 1.5.1 && \
    cd pico-sdk/ && \
    git submodule update --init && \
    cd /
    
# Set the Pico SDK environment variable
ENV PICO_SDK_PATH=/pico/pico-sdk/
