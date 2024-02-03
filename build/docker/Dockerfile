# Fetch ubuntu image
FROM ubuntu:latest AS base

# Configure mirror. Comment out or update mirror to one that is local to you if build
# times are slow.
RUN sed -i 's/htt[p|ps]:\/\/archive.ubuntu.com\/ubuntu\//http:\/\/mirror.aarnet.edu.au\/pub\/ubuntu\/archive\//g' /etc/apt/sources.list

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
