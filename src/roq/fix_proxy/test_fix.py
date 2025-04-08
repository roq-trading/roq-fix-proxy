#!/usr/bin/env python

from datetime import datetime

import os
import socket

import simplefix

FIX = "FIX.4.4"

SENDER_COMP_ID = "test"
TARGET_COMP_ID = "proxy"

USERNAME = "trader"
PASSWORD = "secret"

ACCOUNT = "A1"
EXCHANGE = "deribit"
SYMBOL = "BTC-PERPETUAL"


def print_message(message):
    print("{}".format(message.decode().replace(chr(1), "|")))


def logon_request():
    msg = simplefix.FixMessage()
    msg.append_pair(8, FIX)
    msg.append_pair(35, "A")
    msg.append_pair(49, SENDER_COMP_ID)
    msg.append_pair(56, TARGET_COMP_ID)
    msg.append_pair(553, USERNAME)
    msg.append_pair(554, PASSWORD)
    msg.append_pair(108, 5)
    msg.append_pair(98, '0')  # none
    msg.append_pair(141, 'Y')  # reset
    request = msg.encode()
    print(request)
    return request


def logout_request():
    msg = simplefix.FixMessage()
    msg.append_pair(8, FIX)
    msg.append_pair(35, "5")
    msg.append_pair(49, SENDER_COMP_ID)
    msg.append_pair(56, TARGET_COMP_ID)
    request = msg.encode()
    print(request)
    return request


def market_data_request():
    msg = simplefix.FixMessage()
    msg.append_pair(8, FIX)
    msg.append_pair(35, "V")
    msg.append_pair(49, SENDER_COMP_ID)
    msg.append_pair(56, TARGET_COMP_ID)
    msg.append_pair(262, "test")
    msg.append_pair(263, 1)  # snapshot+updates
    msg.append_pair(264, 0)  # full book
    msg.append_pair(265, 1)  # incremental
    msg.append_pair(266, "Y")  # aggregated book
    msg.append_pair(267, 2)  # NoMDEntryTypes
    msg.append_pair(269, 0)  # bid
    msg.append_pair(269, 1)  # ask
    msg.append_pair(146, 1)  # NoRelatedSym
    msg.append_pair(55, "BTC-PERPETUAL")
    msg.append_pair(207, "deribit")
    request = msg.encode()
    print(request)
    return request


def new_order_single_request(cl_ord_id):
    msg = simplefix.FixMessage()
    msg.append_pair(8, FIX)
    msg.append_pair(35, "D")
    msg.append_pair(49, SENDER_COMP_ID)
    msg.append_pair(56, TARGET_COMP_ID)
    msg.append_pair(11, cl_ord_id)
    msg.append_pair(1, ACCOUNT)
    msg.append_pair(55, SYMBOL)
    msg.append_pair(207, EXCHANGE)
    msg.append_pair(54, "1")  # buy
    msg.append_pair(40, "2")  # limit
    msg.append_pair(38, 1.0)  # quantity
    msg.append_pair(44, 100.0)  # price
    msg.append_pair(59, "1")  # gtc
    msg.append_pair(60, "19700101-23:59:59")
    request = msg.encode()
    print(request)
    return request


def order_cancel_request(cl_ord_id, orig_cl_ord_id):
    msg = simplefix.FixMessage()
    msg.append_pair(8, FIX)
    msg.append_pair(35, "F")
    msg.append_pair(49, SENDER_COMP_ID)
    msg.append_pair(56, TARGET_COMP_ID)
    msg.append_pair(11, cl_ord_id)
    msg.append_pair(41, orig_cl_ord_id)
    msg.append_pair(1, ACCOUNT)
    msg.append_pair(55, SYMBOL)
    msg.append_pair(207, EXCHANGE)
    msg.append_pair(54, "1")  # buy
    msg.append_pair(38, 1.0)  # quantity
    msg.append_pair(60, "19700101-23:59:59")
    request = msg.encode()
    print(request)
    return request


def order_cancel_replace_request(cl_ord_id, orig_cl_ord_id):
    msg = simplefix.FixMessage()
    msg.append_pair(8, FIX)
    msg.append_pair(35, "G")
    msg.append_pair(49, SENDER_COMP_ID)
    msg.append_pair(56, TARGET_COMP_ID)
    msg.append_pair(11, cl_ord_id)
    msg.append_pair(41, orig_cl_ord_id)
    msg.append_pair(1, ACCOUNT)
    msg.append_pair(55, SYMBOL)
    msg.append_pair(207, EXCHANGE)
    msg.append_pair(54, "1")  # buy
    msg.append_pair(40, "2")  # limit
    msg.append_pair(38, 1.0)  # quantity
    msg.append_pair(44, 200.0)  # quantity
    msg.append_pair(60, "19700101-23:59:59")
    request = msg.encode()
    print(request)
    return request


def request_for_positions_request(pos_req_id, subscription_request_type):
    msg = simplefix.FixMessage()
    msg.append_pair(8, FIX)
    msg.append_pair(35, "AN")
    msg.append_pair(49, SENDER_COMP_ID)
    msg.append_pair(56, TARGET_COMP_ID)
    msg.append_pair(1, ACCOUNT)
    msg.append_pair(207, EXCHANGE)
    msg.append_pair(55, SYMBOL)
    msg.append_pair(263, subscription_request_type)
    msg.append_pair(581, "1")  # account type
    msg.append_pair(710, pos_req_id)
    msg.append_pair(724, "0")  # positions
    msg.append_pair(715, "19700101")  # clearing_business_date
    msg.append_pair(60, "19700101-23:59:59")  # transact_time
    request = msg.encode()
    print_message(request)
    return request


def order_mass_status_request():
    msg = simplefix.FixMessage()
    msg.append_pair(8, FIX)
    msg.append_pair(35, "AF")
    msg.append_pair(49, SENDER_COMP_ID)
    msg.append_pair(56, TARGET_COMP_ID)
    msg.append_pair(1, ACCOUNT)
    msg.append_pair(207, EXCHANGE)
    msg.append_pair(55, SYMBOL)
    msg.append_pair(584, "req_1")
    msg.append_pair(585, "1")  # Status for orders for a security
    request = msg.encode()
    print(request)
    return request


def order_mass_cancel_request():
    msg = simplefix.FixMessage()
    msg.append_pair(8, FIX)
    msg.append_pair(35, "q")
    msg.append_pair(49, SENDER_COMP_ID)
    msg.append_pair(56, TARGET_COMP_ID)
    msg.append_pair(207, EXCHANGE)
    msg.append_pair(55, SYMBOL)
    msg.append_pair(11, "req1")
    msg.append_pair(530, "1")  # 1=security, 7=all orders
    msg.append_pair(60, "19700101-23:59:59")
    request = msg.encode()
    print(request)
    return request


def trade_capture_report_request():
    msg = simplefix.FixMessage()
    msg.append_pair(8, FIX)
    msg.append_pair(35, "AD")
    msg.append_pair(49, SENDER_COMP_ID)
    msg.append_pair(56, TARGET_COMP_ID)
    msg.append_pair(207, EXCHANGE)
    msg.append_pair(55, SYMBOL)
    msg.append_pair(568, "req1")
    msg.append_pair(569, "0")  # 0=all trades
    msg.append_pair(263, "0")  # 0=snapshot
    request = msg.encode()
    print_message(request)
    return request


if __name__ == "__main__":
    home = os.getenv("HOME")
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
        s.connect("{}/run/fix-proxy.sock".format(home))
        # logon
        s.sendall(logon_request())
        response = s.recv(4096)
        print_message(response)
        # s.sendall(order_mass_status_request())
        # response = s.recv(4096)
        # print_message(response)
        # s.sendall(order_mass_status_request())
        # s.sendall(order_mass_cancel_request())
        # s.sendall(trade_capture_report_request())
        s.sendall(request_for_positions_request("pos-1", "0"))  # snapshot
        # s.sendall(request_for_positions_request("pos-2", "1"))  # subscribe
        # s.sendall(new_order_single_request("test-4"))
        # s.sendall(order_cancel_request("test-3", "test-2"))
        # s.sendall(order_cancel_replace_request("test-5", "test-1"))
        # s.sendall(market_data_request())
        response = s.recv(4096)
        print_message(response)
        # request for positions
        # s.sendall(request_for_positions_request("pos-2", "2"))  # unsubscribe
        # response = s.recv(4096)
        # print_message(response)
        # s.sendall(logout_request())
        # response = s.recv(4096)
        # print_message(response)
        # s.sendall(new_order_single_request('test-1'))
        # s.sendall(market_data_request())
        while True:
            response = s.recv(4096)
            if len(response) > 0:
                print_message(response)
