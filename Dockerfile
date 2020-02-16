FROM debian:buster-slim

ADD . /build/

RUN apt-get update && apt-get install -y g++ cmake libpam0g-dev
RUN cd /build && cmake . -DCMAKE_BUILD_TYPE=Release && make -j 4 server tests
RUN cp /build/server /server
RUN cp /build/tests /tests
RUN cp /build/run.sh /run.sh
RUN rm -r /build

ENTRYPOINT ./run.sh
