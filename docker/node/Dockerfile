FROM bademcurrency/badem-env:gcc

ARG NETWORK=live
ARG TRAVIS_TAG=DEV_BUILD
ARG CI_BUILD=OFF
ADD ./ /tmp/src

RUN mkdir /tmp/build && \
    cd /tmp/build && \
    cmake /tmp/src -DCI_BUILD=${CI_BUILD} -DBOOST_ROOT=${BOOST_ROOT} -DACTIVE_NETWORK=badem_${NETWORK}_network && \
    make badem_node -j $(nproc) && \
    make badem_rpc -j $(nproc) && \
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
ENV PATH="${PATH}:/usr/bin"
ENTRYPOINT ["/bin/bash",  "/entry.sh"]
CMD ["badem_node daemon -l"]
