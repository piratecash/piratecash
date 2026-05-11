FROM debian:stretch
LABEL maintainer="PirateCash Developers <dev@piratecash.net>"
LABEL description="Dockerised PirateCashCore, built from Travis"

RUN apt-get update && apt-get -y upgrade && apt-get clean && rm -fr /var/cache/apt/*

COPY bin/* /usr/bin/
