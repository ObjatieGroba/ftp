FROM debian:buster-slim

ADD . /build/

RUN apt-get update && apt-get install -y g++ cmake libpam0g-dev
RUN cd /build && cmake . && make -j 4 ftp && ./ftp
