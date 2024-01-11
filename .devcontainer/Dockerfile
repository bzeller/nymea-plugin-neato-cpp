FROM mcr.microsoft.com/devcontainers/base:jammy

# Environment
ENV DEBIAN_FRONTEND noninteractive
ENV LANGUAGE en_US

# Add repository
RUN apt-get update && apt-get install -y gnupg apt-utils
RUN echo 'deb http://repository.nymea.io jammy main non-free' > /etc/apt/sources.list.d/nymea.list \
    && echo  'deb-src http://repository.nymea.io jammy main non-free' >> /etc/apt/sources.list.d/nymea.list
RUN sudo wget -O /etc/apt/trusted.gpg.d/nymea.gpg https://repository.nymea.io/nymea.gpg

# Install packages
RUN apt update && apt-get upgrade -y
RUN apt-get install -y nymea nymea-cli nymea-app
RUN apt-get install -y --no-install-recommends nymea-sdk
# or use apt-get build-dep nymea-plugin-unifi
RUN apt-get build-dep -y nymea-plugins && setcap -r /usr/bin/nymead
