FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libmysqlclient-dev \
    libhiredis-dev \
    redis-tools \
    mysql-client \
    netcat-openbsd \
    && rm -rf /var/lib/apt/lists/*

COPY . /app
WORKDIR /app/build
RUN cmake .. && make -j$(nproc)

EXPOSE 8080 8081
CMD ["./gateway"]
