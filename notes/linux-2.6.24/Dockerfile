FROM debian:jessie

RUN sed -i 's/httpredir\.deb\(ian\)\?\.org/archive.debian.org/g' /etc/apt/sources.list \
    && sed -i 's/deb\.deb\(ian\)\?\.org/archive.debian.org/g' /etc/apt/sources.list \
    && sed -i 's/security\.deb\(ian\)\?\.org/archive.debian.org/g' /etc/apt/sources.list \
    && sed -i '/jessie-updates/d' /etc/apt/sources.list \
    && sed -i 's/^deb /deb [trusted=yes] /' /etc/apt/sources.list \
    && echo 'Acquire::Check-Valid-Until "false";' > /etc/apt/apt.conf.d/99no-check \
    && echo 'APT::Get::AllowUnauthenticated "true";' >> /etc/apt/apt.conf.d/99no-check

RUN apt-get update \
    && apt-get install -y --force-yes --no-install-recommends \
       gcc g++ make binutils \
       libncurses5-dev bc flex bison \
       busybox-static cpio gzip \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
CMD ["bash"]
