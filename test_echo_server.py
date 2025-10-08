#!/usr/bin/env python

    
"""
    Script to test echo servers sending data in multiple ways.

    In each test, it prints a row with columns:
    
    Send timestamp,finish send timestamp,timestamp of receiving response data, 
    response time,length of data sent,length of data received,thread id,client_id,error flag (0 if no error)
    
    The error is marked when sent data and received data are not equals. Last column set to 1 if error occurs.

    Timestamp values are Unix Time in miliseconds. Time values are in miliseconds.

"""
 
import socket
import sys
import argparse
from argparse import RawTextHelpFormatter
import time
import random
import string
import threading
import select
import math
import logging
from datetime import datetime

__author__ = "Alejandro Ambroa"
__version__ = "1.0.0"
__email__ = "jandroz@gmail.com"

BUFFER_SIZE = 1024
EXIT_FAILURE = 1

program_epilog = (''
    'Table format is:'
    '\n\n'
    'Send timestamp,finish send timestamp,timestamp of receiving response data,' 
    'response time,length of data sent,length of data received,thread id,client_id,error flag (0 if no error)'
    '\n\n'
    'The error is marked when sent data and received data are not equals. Last column set to 1 if error occurs.'
    '\n\n'
    'Timestamp values are Unix Time in miliseconds. Time values are in miliseconds.')

class EchoClient:
    INIT = 0
    SENDING = 1
    SENT = 2
    READING = 3
    READY = 4
    CLOSED = 5
    def __init__(self, id : str, p_socket, client_state : int, scheduled : float):
        self.id = id
        self.socket = p_socket
        self.scheduled = scheduled
        self.state = client_state
        self.data_to_send = None
        self.bytes_to_send = 0
        self.data_received = bytearray()
        self.bytes_sent = 0
        self.bytes_received = 0
        self.messages = 0
        self.send_timestamp = 0
        self.last_send_timestamp = 0
        self.compensated_timestamp = 0
        pass

class EchoDebugLogger:
    def __init__(self):
        console_handler1 = logging.StreamHandler()
        console_handler1.setFormatter(logging.Formatter('%(asctime)s - %(message)s'))
        self.mainLogger = logging.getLogger(__name__)
        self.mainLogger.addHandler(console_handler1)

        console_handler2 = logging.StreamHandler()
        console_handler2.setFormatter(logging.Formatter('%(asctime)s - id: %(id)-4s fileno: %(fileno)-4s - scheduled %(scheduled)-12s - %(message)s'))
        self.clientLogger = logging.getLogger('client')
        self.clientLogger.addHandler(console_handler2)

    def client_log(self, client: EchoClient, msg : str):
        self.clientLogger.debug(msg, extra=client_info_to_dict(client))

    def main_log(self, msg : str):
        self.mainLogger.debug(msg)

def print_table_row(send_timestamp, finish_send_timestamp, response_time, data_sent_length, data_received_length, thread_id, client_id, error):
    print('%d,%d,%d,%d,%d,%d,%s,%i' % (send_timestamp * 1000, finish_send_timestamp * 1000, 
                    response_time * 1000, data_sent_length, data_received_length, thread_id, client_id, 1 if error else 0))

def connect_echo_client(client, host, port):
    client.socket.connect((host, port))
    client.socket.setblocking(0)
    client.state = EchoClient.READY

def rand_interval_range(min_interval, max_interval):
    return random.randint(min_interval, max_interval) / 1000.0

def s_key(socket):
    return socket.fileno()

def scheduled_to_str(client : EchoClient):
    return datetime.fromtimestamp(client.scheduled).strftime("%H:%M:%S,%m")

def client_info_to_dict(client : EchoClient):
    return {
        'id': client.id,
        'fileno': client.socket.fileno(),
        'scheduled': scheduled_to_str(client)
    }

def print_error(msg : str):
    print(msg, file=sys.stderr)
    
def test_echo(logger, host, port, max_messages, data_length, min_interval, max_interval, sockets_by_thread):

    thread_id = threading.get_native_id()

    inputs = []
    outputs = []
    clients = {}

    # create clients and schedule.
    t1 = time.time()
    schedule = t1
    for sock_index in range(0, sockets_by_thread):
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        client = EchoClient(str(sock_index), s, EchoClient.INIT, schedule)
        clients[s_key(s)] = client
        logger.client_log(client, 'Client created.')
        schedule = schedule + rand_interval_range(min_interval, max_interval)
    
    wait_interval = 0

    while True:
        current_time = time.time()

        # check if there is an operation that need to be executed (connect or write new message).
        min_schedule = math.inf
        clients_to_delete = []
        for echo_client in clients.values():
            if echo_client.state == EchoClient.READY and echo_client.messages >= max_messages:
                inputs.remove(echo_client.socket)
                clients_to_delete.append(s_key(echo_client.socket))
                echo_client.socket.close()
                echo_client.state = EchoClient.CLOSED
            else:
                if echo_client.scheduled <= current_time:
                    if echo_client.state == EchoClient.INIT:
                        connect_echo_client(echo_client, host, port)
                        inputs.append(echo_client.socket)
                        outputs.append(echo_client.socket)
                        logger.client_log(echo_client, 'Client connected.')
                    elif echo_client.state == EchoClient.READY:
                        outputs.append(echo_client.socket)

                if echo_client.scheduled < min_schedule:
                    min_schedule = echo_client.scheduled

        for cd in clients_to_delete:
            del clients[cd]

        if not clients:
            break
        
        ct = time.time()
        for echo_client in clients.values():
            if echo_client.state in (EchoClient.SENDING, EchoClient.SENT):
                echo_client.compensated_timestamp += (ct - current_time)


        next_schedule = min_schedule - current_time
        wait_interval = None if next_schedule < 0 else max(0, next_schedule)
            
        readable, writable, exceptional = select.select(inputs, outputs, inputs + outputs, wait_interval)

        for readable_socket in readable:
            echo_client = clients[s_key(readable_socket)]
            echo_client.state = EchoClient.READING
            recv_data = echo_client.socket.recv(BUFFER_SIZE)
            echo_client.data_received += recv_data 
            logger.client_log(echo_client, 'Data received.')
            if len(echo_client.data_received) == echo_client.bytes_to_send:
                received_timestamp = time.time()
                echo_client.state = EchoClient.READY
                echo_client.scheduled = received_timestamp + rand_interval_range(min_interval, max_interval)
                logger.client_log(echo_client, 'Data complete. Message processed.')
                # here prints result table rows.
                print_table_row(echo_client.send_timestamp, received_timestamp, received_timestamp - echo_client.send_timestamp - echo_client.compensated_timestamp, 
                                        echo_client.bytes_sent, len(echo_client.data_received), thread_id, echo_client.id, 0)
                echo_client.data_received.clear()

        for writable_socket in writable:
            echo_client = clients[s_key(writable_socket)]
            if echo_client.state == EchoClient.READY:
                echo_client.data_to_send = bytearray(''.join([random.choice(string.ascii_uppercase) 
                                                          for i in range(0, data_length - 1)]), 'utf-8') + b'\n'
                echo_client.bytes_to_send = len(echo_client.data_to_send)
                echo_client.bytes_sent = 0
                echo_client.send_timestamp = time.time()
                echo_client.last_send_timestamp = echo_client.compensated_timestamp = 0
                echo_client.state = EchoClient.SENDING
            
            sent = writable_socket.send(echo_client.data_to_send[echo_client.bytes_sent:])
            echo_client.last_send_timestamp = time.time()
            echo_client.bytes_sent += sent
            print('send')
            logger.client_log(echo_client, 'Client sends data')

            if echo_client.bytes_sent == echo_client.bytes_to_send:
                echo_client.state = EchoClient.SENT
                echo_client.schedule = -1
                echo_client.messages += 1
                outputs.remove(echo_client.socket)
                logger.client_log(echo_client, 'Client finish send data')


    print('Tests finished')

if __name__ == '__main__':

    logging.basicConfig(level=logging.INFO, handlers=[])

    logger = EchoDebugLogger()

    parser = argparse.ArgumentParser(
            prog='test_echo_server',
            description='Tests echo servers sending generated variable data.',
            epilog=program_epilog, formatter_class=RawTextHelpFormatter)

    parser.add_argument('host', type=str, help='host or ip of the  server to test', )
    parser.add_argument('port', type=int, help='port')

    parser.add_argument('-l', '--length', type=int, required=False, default=64, help='length of string to send.')
    parser.add_argument('-i', '--interval_range', type=str, required=False, default='0-250', 
                        help='interval range (format: [min-]<max>), in miliseconds, between each send of a string. This includes connections too.' \
                        ' Script selects a random number between min and max')
    parser.add_argument('-n', '--num', type=int, required=False, default=100, help='Num messages to send')
    parser.add_argument('-p', '--threads', type=int, required=False, default=1, help='Num. of threads')
    parser.add_argument('-c', '--connections', type=int, required=False, default=1, help='Connections by thread.')
   

    args = parser.parse_args()

    if args.port <= 0:
        print_error('Port must be greater than 0')
        exit(EXIT_FAILURE)

    if args.length < 0:
        print_error('Length of string must be a positive number')
        exit(EXIT_FAILURE)

    if args.num <= 0:
        print_error('Num of messages must be greater than 0')
        exit(EXIT_FAILURE)

    if args.threads <= 0:
        print_error('Threads number must be greather than 0')
        exit(EXIT_FAILURE)

    if args.connections <= 0:
        print_error('Connections number must be greather than 0')
        exit(EXIT_FAILURE)

    min_interval = max_interval = 0

    try:
        interval_spec = args.interval_range.split('-')

        if len(interval_spec) == 1:
            min_interval = max_interval = int(interval_spec[0])
        else:
            max_interval = int(interval_spec[1])
            min_interval = max_interval if not interval_spec[0] else int(interval_spec[0])
    except:
        print_error("Error in interval range param specification. Format is: [min-]<max>. Examples: 100-250, or 300 (which equivalent to 300-300)")
        exit(EXIT_FAILURE)
    
    if min_interval > max_interval:
        print_error('max interval must be greather or equal than min interval')
        exit(EXIT_FAILURE)

    for i in range(0, args.threads):
        pt = threading.Thread(target=test_echo, args=(logger, args.host, args.port, args.num, 
                                                args.length, min_interval, max_interval, args.connections))
        pt.start()
    
