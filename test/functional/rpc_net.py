#!/usr/bin/env python3
# Copyright (c) 2017 The Bitcoin Core developers
# Copyright (c) 2020-2022 The Cosanta Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test RPC calls related to net.

Tests correspond to code in rpc/net.cpp.
"""

from test_framework.test_framework import DashTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than_or_equal,
    assert_greater_than,
    assert_raises_rpc_error,
    connect_nodes,
    p2p_port,
    wait_until,
)
from test_framework.mininode import P2PInterface
from test_framework.messages import (
    CAddress,
    msg_addr,
    NODE_NETWORK,
    NODE_GETUTXO,NODE_BLOOM,
    NODE_NETWORK_LIMITED,
)


def assert_net_servicesnames(servicesflag, servicenames):
    """Utility that checks if all flags are correctly decoded in
    `getpeerinfo` and `getnetworkinfo`.

    :param servicesflag: The services as an integer.
    :param servicesnames: The list of decoded services names, as strings.
    """
    if servicesflag & NODE_NETWORK:
        assert "NETWORK" in servicenames
    if servicesflag & NODE_GETUTXO:
        assert "GETUTXO" in servicenames
    if servicesflag & NODE_BLOOM:
        assert "BLOOM" in servicenames
    if servicesflag & NODE_NETWORK_LIMITED:
        assert "NETWORK_LIMITED" in servicenames


class NetTest(BitcoinTestFramework):
    def set_test_params(self):
        self.set_dash_test_params(3, 1, fast_dip3_enforcement=True)

    def run_test(self):
        # Wait for one ping/pong to finish so that we can be sure that there is no chatter between nodes for some time
        # Especially the exchange of messages like getheaders and friends causes test failures here
        self.nodes[0].ping()
        wait_until(lambda: all(['pingtime' in n for n in self.nodes[0].getpeerinfo()]))
        self.log.info('Connect nodes both way')
        connect_nodes(self.nodes[0], 1)
        connect_nodes(self.nodes[1], 0)

        self._test_connection_count()
        self._test_getnettotals()
        self._test_getnetworkinfo()
        self._test_getaddednodeinfo()
        self._test_getpeerinfo()
        self.test_service_flags()
        self._test_getnodeaddresses()

    def _test_connection_count(self):
        # connect_nodes connects each node to the other
        # and node0 was also connected to node2 (a masternode)
        # during network setup
        assert_equal(self.nodes[0].getconnectioncount(), 3)

    def _test_getnettotals(self):
        # getnettotals totalbytesrecv and totalbytessent should be
        # consistent with getpeerinfo. Since the RPC calls are not atomic,
        # and messages might have been recvd or sent between RPC calls, call
        # getnettotals before and after and verify that the returned values
        # from getpeerinfo are bounded by those values.
        net_totals_before = self.nodes[0].getnettotals()
        peer_info = self.nodes[0].getpeerinfo()
        net_totals_after = self.nodes[0].getnettotals()
        assert_equal(len(peer_info), 3)
        peers_recv = sum([peer['bytesrecv'] for peer in peer_info])
        peers_sent = sum([peer['bytessent'] for peer in peer_info])

        assert_greater_than_or_equal(peers_recv, net_totals_before['totalbytesrecv'])
        assert_greater_than_or_equal(net_totals_after['totalbytesrecv'], peers_recv)
        assert_greater_than_or_equal(peers_sent, net_totals_before['totalbytessent'])
        assert_greater_than_or_equal(net_totals_after['totalbytessent'], peers_sent)

        # test getnettotals and getpeerinfo by doing a ping
        # the bytes sent/received should change
        # note ping and pong are 32 bytes each
        self.nodes[0].ping()
        wait_until(lambda: (self.nodes[0].getnettotals()['totalbytessent'] >= net_totals_after['totalbytessent'] + 32 * 2), timeout=1)
        wait_until(lambda: (self.nodes[0].getnettotals()['totalbytesrecv'] >= net_totals_after['totalbytesrecv'] + 32 * 2), timeout=1)

        peer_info_after_ping = self.nodes[0].getpeerinfo()
        for before, after in zip(peer_info, peer_info_after_ping):
            assert_greater_than_or_equal(after['bytesrecv_per_msg']['pong'], before['bytesrecv_per_msg']['pong'] + 32)
            assert_greater_than_or_equal(after['bytessent_per_msg']['ping'], before['bytessent_per_msg']['ping'] + 32)

    def _test_getnetworkinfo(self):
        assert_equal(self.nodes[0].getnetworkinfo()['networkactive'], True)
        assert_equal(self.nodes[0].getnetworkinfo()['connections'], 3)

        self.nodes[0].setnetworkactive(state=False)
        assert_equal(self.nodes[0].getnetworkinfo()['networkactive'], False)
        # Wait a bit for all sockets to close
        wait_until(lambda: self.nodes[0].getnetworkinfo()['connections'] == 0, timeout=3)

        self.nodes[0].setnetworkactive(state=True)
        self.log.info('Connect nodes both way')
        connect_nodes(self.nodes[0], 1)
        connect_nodes(self.nodes[1], 0)

        assert_equal(self.nodes[0].getnetworkinfo()['networkactive'], True)
        assert_equal(self.nodes[0].getnetworkinfo()['connections'], 2)

        # check the `servicesnames` field
        network_info = [node.getnetworkinfo() for node in self.nodes]
        for info in network_info:
            assert_net_servicesnames(int(info["localservices"]), info["localservicesnames"])

        self.log.info('Test extended connections info')
        connect_nodes(self.nodes[1], 2)
        self.nodes[1].ping()
        wait_until(lambda: all(['pingtime' in n for n in self.nodes[1].getpeerinfo()]))
        assert_equal(self.nodes[1].getnetworkinfo()['connections'], 3)
        assert_equal(self.nodes[1].getnetworkinfo()['inboundconnections'], 1)
        assert_equal(self.nodes[1].getnetworkinfo()['outboundconnections'], 2)
        assert_equal(self.nodes[1].getnetworkinfo()['mnconnections'], 1)
        assert_equal(self.nodes[1].getnetworkinfo()['inboundmnconnections'], 0)
        assert_equal(self.nodes[1].getnetworkinfo()['outboundmnconnections'], 1)

    def _test_getaddednodeinfo(self):
        assert_equal(self.nodes[0].getaddednodeinfo(), [])
        # add a node (node2) to node0
        ip_port = "127.0.0.1:{}".format(p2p_port(2))
        self.nodes[0].addnode(node=ip_port, command='add')
        # check that the node has indeed been added
        added_nodes = self.nodes[0].getaddednodeinfo(ip_port)
        assert_equal(len(added_nodes), 1)
        assert_equal(added_nodes[0]['addednode'], ip_port)
        # check that a non-existent node returns an error
        assert_raises_rpc_error(-24, "Node has not been added", self.nodes[0].getaddednodeinfo, '1.1.1.1')

    def _test_getpeerinfo(self):
        peer_info = [x.getpeerinfo() for x in self.nodes]
        # check both sides of bidirectional connection between nodes
        # the address bound to on one side will be the source address for the other node
        assert_equal(peer_info[0][0]['addrbind'], peer_info[1][0]['addr'])
        assert_equal(peer_info[1][0]['addrbind'], peer_info[0][0]['addr'])
        # check the `servicesnames` field
        for info in peer_info:
            assert_net_servicesnames(int(info[0]["services"]), info[0]["servicesnames"])

    def test_service_flags(self):
        self.nodes[0].add_p2p_connection(P2PInterface(), services=(1 << 4) | (1 << 63))
        assert_equal(['UNKNOWN[2^4]', 'UNKNOWN[2^63]'], self.nodes[0].getpeerinfo()[-1]['servicesnames'])
        self.nodes[0].disconnect_p2ps()

    def _test_getnodeaddresses(self):
        self.nodes[0].add_p2p_connection(P2PInterface())

        # send some addresses to the node via the p2p message addr
        msg = msg_addr()
        imported_addrs = []
        for i in range(256):
            a = "123.123.123.{}".format(i)
            imported_addrs.append(a)
            addr = CAddress()
            addr.time = 100000000
            addr.nServices = NODE_NETWORK
            addr.ip = a
            addr.port = 8333
            msg.addrs.append(addr)
        self.nodes[0].p2p.send_and_ping(msg)

        # obtain addresses via rpc call and check they were ones sent in before
        REQUEST_COUNT = 10
        node_addresses = self.nodes[0].getnodeaddresses(REQUEST_COUNT)
        assert_equal(len(node_addresses), REQUEST_COUNT)
        for a in node_addresses:
            # see penalty calculations for ADDRs with nTime <= 100000000 in net_processing.cpp
            assert_equal(a["time"], self.mocktime - 5 * 24 * 60 * 60 - 2 * 60 * 60)
            assert_equal(a["services"], NODE_NETWORK)
            assert a["address"] in imported_addrs
            assert_equal(a["port"], 8333)

        assert_raises_rpc_error(-8, "Address count out of range", self.nodes[0].getnodeaddresses, -1)

        # addrman's size cannot be known reliably after insertion, as hash collisions may occur
        # so only test that requesting a large number of addresses returns less than that
        LARGE_REQUEST_COUNT = 10000
        node_addresses = self.nodes[0].getnodeaddresses(LARGE_REQUEST_COUNT)
        assert_greater_than(LARGE_REQUEST_COUNT, len(node_addresses))


if __name__ == '__main__':
    NetTest().main()
