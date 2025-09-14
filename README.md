This project implements a multi-threaded daemon that simulates network traffic handling and message processing using a thread-safe ring buffer.

Core Components:

Ring Buffer (ringbuf.c):
A circular buffer implementation with mutex and condition variable support to ensure safe concurrent reads and writes between producer and consumer threads.

Daemon (daemon.c):
1.Simulates incoming network packets from multiple connections.
2.Writer threads: Each thread reads data from input files (representing client messages), splits them into packets, and writes them into the ring buffer with randomized delays to simulate real-world network latency.
3.Reader threads: These threads continuously read packets from the ring buffer, filter unwanted or malicious data, and store valid messages into output files (named after the destination port).


Features:
1.Thread-safe communication via a shared ring buffer.
2.Simulation of packet-based network transmission with randomized timing.
3.Filtering logic for invalid or malicious packets.
4.Parallel message processing using multiple consumer threads.
5.Automatic persistence of processed messages into output files.
