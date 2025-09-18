#!/usr/bin/env python

    
"""
    Script to test echo servers sending data in multiple ways.

    In each test, it prints a row with columns:
    
    Send timestamp, finish send timestamp, timestamo of receiving response data, 
    response time, length of data sent, length of data received, thread id, error flag (0 if no error)
    
    The error is marked when sent data and received data are not equals. Last column set to 1 if error.

"""
 
import socket
import sys
import argparse
from argparse import RawTextHelpFormatter
import time
import random
import string
import threading

_author__ = "Alejandro Ambroa"
__version__ = "1.0.0"
__email__ = "jandroz@gmail.com"


program_epilog = (''
    'Table format is:'
    '\n\n'
    'Send timestamp,finish send timestamp,timestamp of receiving response data,' 
    'response time,length of data sent,length of data received,thread id,error flag (0 if no error)'
    '\n\n'
    'The error is marked when sent data and received data are not equals. Last column set to 1 if error')


def print_msg(msg):
    print('%f, %s' % (time.time(), msg), file=sys.stderr)


def print_table_row(send_timestamp, finish_send_timestamp, current_time, response_time, data_length, data_len_received, thread_id, error):
    print('%f,%f,%f,%f,%d,%d,%d,%i' % (send_timestamp, finish_send_timestamp, current_time, 
                    response_time, data_length, data_len_received, thread_id, 1 if error else 0))

def print_avg_time(thread_id, acc_time, n_msg):
    print('Average response time from thread %d: %f ms' % (thread_id, acc_time / n_msg,))
     
def test_echo(host, port, max_messages, dlength, generate_table, interval):

    thread_id = threading.get_native_id()
    
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:

        data_length = dlength + 1
        average = 0.0
        n_msg = 0
        time_elapsed = 0.0
        acc_response_time = 0.0

        print_msg('Connecting to %s:%d...' % (host, port))

        s.connect((args.host, args.port))

        print_msg('Connected. Sending messages...')

        t2 = time.time()

        while not error and n_msg < max_messages:
            n_msg += 1
            data_received = b''
            retry = 0

            data = bytes(''.join([random.choice(string.ascii_uppercase) 
                for i in range(0, data_length - 1)]), 'utf-8') + b'\n'

            send_timestamp = time.time()
            s.sendall(data)
            finish_send_timestamp = time.time()

            data_completed = False
            data_len_received = 0

            t1 = time.time()

            error = False

            while (not data_completed):
                retry += 1

                data_from_server = s.recv(1024) #min(len(data) - data_len_received, 1024))

                if not data_from_server:
                    print_msg('Server close connection')
                    error = True
                    break

                data_received += data_from_server
            
                #print("Received fragment %s" % (data_from_server,))
                #print("Fragment size of %d bytes" % (len(data_from_server),))

                data_len_received = len(data_received)
                data_completed = (data_received == data)

            current_time = time.time()
            response_time = (current_time - t1) * 1000.0
            acc_response_time += response_time

            if generate_table:
                print_table_row(send_timestamp, finish_send_timestamp, current_time, 
                    response_time, data_length, data_len_received, thread_id, error)
            else:
                time_elapsed = (current_time - t2)
                if time_elapsed >= 5:
                    print_avg_time(thread_id, acc_response_time, n_msg)
                    t2 = current_time
                    time_elapsed = 0

            # wait interval
            sleep_time = interval - response_time
            if sleep_time > 0:
                time.sleep(sleep_time / 1000.0)
        
        s.close()

        if not generate_table:
            print_avg_time(thread_id, acc_response_time, n_msg)

    print('Test thread %d finished' % (thread_id,))

if __name__ == '__main__':

    parser = argparse.ArgumentParser(
            prog='test_echo_server',
            description='Tests echo servers sending generated variable data.',
            epilog=program_epilog, formatter_class=RawTextHelpFormatter)

    parser.add_argument('host', type=str, help='host or ip of the  server to test', )
    parser.add_argument('port', type=int, help='port')

    parser.add_argument('-l', '--length', type=int, required=False, default=64, help='length of string to send. If value is zero, tester connect and disconnect.')
    parser.add_argument('-i', '--interval', type=float, required=False, default=1000, help='interval, in miliseconds, between each send of a string. Can be zero.')
    parser.add_argument('-r', '--num', type=int, required=False, default=100, help='Num messages to send')
    parser.add_argument('-t', '--table', action='store_true', default=False, help='Generate table data (num_message, response time) instead prints average response time')
    parser.add_argument('-c', '--concurrency', type=int, required=False, default=2, help='Concurrent connections')
    args = parser.parse_args()

    if args.port <= 0:
        print('Port must be greater than 0')
        exit(1)

    if args.length < 0:
        print('Length of string must be a positive number')
        exit(1)

    if args.interval < 0:
        print('Interval must be a positive number')
        exit(1)

    if args.num <= 0:
        print('Num of messages must be greater than 0')
        exit(1)

    if args.concurrency <= 0:
        print('Concurrent connections must be greather than 0')
        exit(1)

    for i in range(0, args.concurrency):
        pt = threading.Thread(target=test_echo, args=(args.host, args.port, args.num, args.length, args.table, args.interval))
        pt.start()
        time.sleep(random.random() * (args.interval / 1000.0))


