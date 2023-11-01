# syntax=docker/dockerfile:1

FROM iorfanidi/ubuntu-20.04-gcc-cmake-git:latest
RUN apt-get update && apt-get install -y gdb

COPY . /libuv/docs/code

RUN cmake -E make_directory /libuv/docs/code/build

WORKDIR /libuv/docs/code/build


RUN cmake .. -DBUILD_TESTING=ON
RUN cmake --build .