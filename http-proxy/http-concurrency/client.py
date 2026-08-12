import socket

sock = socket.socket()
sock.connect(('0.0.0.0', 8082))

result = sock.send(b'GET / HTTP/1.1\r\nHost:localhost:8081\r\n\r\n')
print("result is")
print(result)
recv_result = sock.recv(4096)
print('recv result is')
print(recv_result)