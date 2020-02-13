FROM debian:buster-slim

ADD . /build/

RUN apt-get update && apt-get install -y g++ cmake libpam0g-dev
RUN cd /build && cmake . -DCMAKE_BUILD_TYPE=Release && make -j 4 ftp
RUN cp /build/ftp /ftp
RUN rm -r /build

ENTRYPOINT ./ftp
