FROM ubuntu:latest
LABEL version="0.4" description="Mosquitto and OwnTracks Recorder"
MAINTAINER Jan-Piet Mens <jpmens@gmail.com>

RUN apt-get install -y software-properties-common && \
	apt-add-repository ppa:mosquitto-dev/mosquitto-ppa && \
	apt-get update && \
	apt-get install -y \
		build-essential \
		git-core \
		libmosquitto-dev \
		libcurl3 \
		libcurl4-openssl-dev \
		liblua5.2-dev \
		mosquitto \
		mosquitto-clients \
		supervisor \
		wget \
		&& \
	apt-get clean && \
	rm -rf /var/lib/apt/lists/*

RUN groupadd --system owntracks && \
	adduser --system --disabled-password --disabled-login owntracks

# data volume
RUN mkdir -p -m 775 /owntracks && \
	chown owntracks:owntracks /owntracks
VOLUME /owntracks

# Recorder
RUN mkdir -p /usr/local/src /var/log/supervisor
WORKDIR /usr/local/src
RUN git clone https://github.com/owntracks/recorder.git
WORKDIR /usr/local/src/recorder
COPY config.mk /usr/local/src/recorder/config.mk
RUN make && make install
RUN chown owntracks /usr/local/bin/ocat /usr/local/sbin/ot-recorder && \
    chgrp owntracks /usr/local/bin/ocat /usr/local/sbin/ot-recorder && \
    chmod 7111 /usr/local/bin/ocat /usr/local/sbin/ot-recorder


COPY launcher.sh /usr/local/sbin/launcher.sh
COPY generate-CA.sh /usr/local/sbin/generate-CA.sh
RUN chmod 755 /usr/local/sbin/launcher.sh /usr/local/sbin/generate-CA.sh
COPY supervisord.conf /etc/supervisor/conf.d/supervisord.conf
COPY mosquitto.conf mosquitto.acl /etc/mosquitto/

EXPOSE 1883 8883 8083
CMD ["/usr/local/sbin/launcher.sh"]
