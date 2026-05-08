FROM debian:stretch
LABEL maintainer="Cosanta Developers <dev@cosa.is>"
LABEL description="Dockerised CosantaCore, built from Travis"

RUN apt-get update && apt-get -y upgrade && apt-get clean && rm -fr /var/cache/apt/*

COPY bin/* /usr/bin/
