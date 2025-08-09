
FROM ubuntu:22.04

RUN apt-get update && \
    apt-get install -y build-essential gcc make libpthread-stubs0-dev

WORKDIR /app

COPY . /app

RUN gcc -o proxyServerWithCache proxyServerWithCache.c proxy_parse.c -lpthread

EXPOSE 8080

CMD ["./proxyServerWithCache", "8080"]