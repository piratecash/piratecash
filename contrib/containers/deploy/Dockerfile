FROM phusion/baseimage:focal-1.0.0
LABEL maintainer="PirateCash Developers <dev@piratecash.net>"
LABEL description="Dockerised PirateCashCore, built from Travis"

ARG USER_ID
ARG GROUP_ID

ENV HOME /home/piratecash

# add user with specified (or default) user/group ids
ENV USER_ID ${USER_ID:-1000}
ENV GROUP_ID ${GROUP_ID:-1000}
RUN groupadd -g ${GROUP_ID} piratecash && \
    useradd -u ${USER_ID} -g piratecash -s /bin/bash -m -d /home/piratecash piratecash && \
    mkdir /home/piratecash/.piratecore && \
    chown ${USER_ID}:${GROUP_ID} -R /home/piratecash

COPY bin/* /usr/local/bin/

RUN chmod a+x /usr/local/bin/*

USER piratecash

VOLUME ["/home/piratecash"]

EXPOSE 19606 63636 19696 50666

WORKDIR /home/piratecash
