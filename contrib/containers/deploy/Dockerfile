FROM phusion/baseimage:focal-1.0.0
LABEL maintainer="Cosanta Developers <dev@cosa.is>"
LABEL description="Dockerised CosantaCore, built from Travis"

ARG USER_ID
ARG GROUP_ID

ENV HOME /home/cosanta

# add user with specified (or default) user/group ids
ENV USER_ID ${USER_ID:-1000}
ENV GROUP_ID ${GROUP_ID:-1000}
RUN groupadd -g ${GROUP_ID} cosanta && \
    useradd -u ${USER_ID} -g cosanta -s /bin/bash -m -d /home/cosanta cosanta && \
    mkdir /home/cosanta/.cosantacore && \
    chown ${USER_ID}:${GROUP_ID} -R /home/cosanta

COPY bin/* /usr/local/bin/

RUN chmod a+x /usr/local/bin/*

USER cosanta

VOLUME ["/home/cosanta"]

EXPOSE 9606 60606 9696 60696

WORKDIR /home/cosanta
