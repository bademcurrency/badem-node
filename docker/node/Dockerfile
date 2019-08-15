FROM ubuntu:16.04

ARG NETWORK=live

ENV BOOST_ROOT=/tmp/boost_install

ADD ci /tmp/ci
ADD util /tmp/util

RUN /tmp/util/build_prep/update-common && /tmp/util/build_prep/ubuntu/prep.sh

ADD ./ /tmp/src

RUN mkdir /tmp/build && \
    cd /tmp/build && \
    cmake /tmp/src -DBOOST_ROOT=${BOOST_ROOT} -DACTIVE_NETWORK=badem_${NETWORK}_network && \
    make badem_node && \
    make badem_rpc && \
    cd .. && \
    echo ${NETWORK} > /etc/badem-network

FROM ubuntu:16.04
COPY --from=0 /tmp/build/badem_node /usr/bin
COPY --from=0 /tmp/build/badem_rpc /usr/bin
COPY --from=0 /etc/badem-network /etc
COPY docker/node/entry.sh /entry.sh
COPY docker/node/config /usr/share/badem/config
RUN chmod +x /entry.sh
RUN ln -s /usr/bin/badem_node /usr/bin/bdm_node
CMD ["/bin/bash",  "/entry.sh"]
