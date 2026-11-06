import sys
from datetime import datetime as dt
from socket import inet_ntop, AF_INET

f = open('lossy.pcap', 'rb')
magic_number = f.read(4)
major_version = f.read(2)
minor_version = f.read(2)
timezone_offset = f.read(4)
timestamp_accuracy = f.read(4)
snapshot_length = f.read(4)
link_layer_header_type = f.read(4)

print(f'number is {int.from_bytes(magic_number, sys.byteorder):x}')
print(f'major version is {int.from_bytes(major_version, sys.byteorder):x}')
print(f'minor version is {int.from_bytes(minor_version, sys.byteorder):x}')
print(f'timezone offset is {int.from_bytes(timezone_offset, sys.byteorder)}')
print(f'timestamp accuracy is {int.from_bytes(timestamp_accuracy, sys.byteorder)}')
print(f'snapshot length is {int.from_bytes(snapshot_length, sys.byteorder)}')
print(f'link layer header type is {int.from_bytes(link_layer_header_type, sys.byteorder)}\n')

timestamp_seconds = f.read(4)
timestamp_microseconds = f.read(4)
captured_packet_data_length = f.read(4)
untruncated_packet_data_length = f.read(4)

print(f'timestamps in seconds {int.from_bytes(timestamp_seconds, sys.byteorder)}')
print(f'timestamps in microseconds {int.from_bytes(timestamp_microseconds, sys.byteorder)}')
print(f'captured packet data length {int.from_bytes(captured_packet_data_length, sys.byteorder)}')
print(f'untruncated captured packet data length {int.from_bytes(untruncated_packet_data_length, sys.byteorder)}\n')

# print(f'timestamp as date {dt.fromtimestamp(int.from_bytes(timestamp_seconds, sys.byteorder))}')

packet_raw = f.read(int.from_bytes(captured_packet_data_length, sys.byteorder))
print(packet_raw.hex('\\'))

dmac_destination_mac_address = packet_raw[0:6]
smac_destination_mac_address = packet_raw[6:12]
print(len(smac_destination_mac_address))
type_protocol_type = packet_raw[12:14]

ip_packet_raw = packet_raw[14:]

# start of ip header processing -----

version_ihl = ip_packet_raw[0]
version = version_ihl >> 4
ihl = version_ihl & 0x0f

type_of_service = ip_packet_raw[1]
total_length = ip_packet_raw[2:4]
identification = ip_packet_raw[4:6]
flags_fragment_offset_raw = ip_packet_raw[6:8]
flags = flags_fragment_offset_raw[0] >> 5
fragment_offset = int.from_bytes(flags_fragment_offset_raw, sys.byteorder) & 0x1fff
ttl = ip_packet_raw[8]
protocol = ip_packet_raw[9]
header_checksum = ip_packet_raw[10:12]
source_address = ip_packet_raw[12:16]
destination_address = ip_packet_raw[16:20]

print(f'ip protocol version is {version}')
print(f'IHL (internet header length) is {ihl}')
print(f'Type of Service: precedence = {type_of_service >> 5}, \
        Delay = {'Normal' if (type_of_service >> 4) & 0b1 == 0 else 'Low' } \
        Throughput = {'Normal' if (type_of_service >> 3) & 0b1 == 0 else 'High'} \
        Relibility = {'Normal' if (type_of_service >> 2) & 0b1 == 0 else 'High'}')
print(f'total length is {int.from_bytes(total_length, 'big')}')
print(f'identificaiton is {identification}')
print(f'flags: reserved (must be zero) = {flags & 0b100}; DF = {flags & 0b010}; MF = {flags & 0b1}')
print(f'fragment offset is {fragment_offset}')
print(f'ttl is {ttl}')
print(f'protocol is {protocol}')
print(f'header checksum is {int.from_bytes(header_checksum, 'big')}')
print(f'source address is {inet_ntop(AF_INET, source_address)}')
print(f'destination address is {inet_ntop(AF_INET, destination_address)}')
print('\n')

# tcp packet raw must be of length 44
tcp_packet_raw = ip_packet_raw[20:]
source_port = tcp_packet_raw[0:2]
destination_port = tcp_packet_raw[2:4]
sequence_number = tcp_packet_raw[4:8]
acknowledgment_number = tcp_packet_raw[8:12]
data_offset_reserved_flags = tcp_packet_raw[12:14]
data_offset = int.from_bytes(data_offset_reserved_flags, 'big') >> 12
reserved = (int.from_bytes(data_offset_reserved_flags, 'big') >> 6) & 0b111111
flags = int.from_bytes(data_offset_reserved_flags, 'big') & 0b111111
window = tcp_packet_raw[14:16]
checksum = tcp_packet_raw[16:18]
urgent_pointer = tcp_packet_raw[18:20]
options_padding = tcp_packet_raw[20:]




print(f'TCP Source Port is {int.from_bytes(source_port, 'big')}')
print(f'TCP Destination Port is {int.from_bytes(destination_port, 'big')}')
print(f'TCP Sequence Number is {int.from_bytes(sequence_number, 'big')}')
print(f'TCP Acknowledgment Number is {int.from_bytes(acknowledgment_number, 'big')}')
print(f'TCP Data Offset is {data_offset}')
print(f'TCP Reserved is {reserved}')
print(f'TCP Flags are: Urgent - {True if flags & 0b100000 else False}, \
      ACK - {True if flags & 0b010000 else False} \
      PSH - {True if flags & 0b001000 else False},\
      RST - {True if flags & 0b000100 else False},\
      SYN - {True if flags & 0b000010 else False} \
      FIN - {True if flags & 0b000001 else False}')
print(f'TCP Window is {int.from_bytes(window, 'big')}')
print(f'TCP Checksum is {int.from_bytes(checksum, 'big')}')
print(f'TCP Urgent Pointer is {int.from_bytes(urgent_pointer, 'big')}')

print("Options and padding are:")
print(options_padding.hex('\\'))

options_size = (data_offset - 5) * 32

if options_size == 0:
      pass

maximum_segment_size = 0
window_scale = 0
timestamp_value = 0
timestamp_echo_reply = 0
sack_permitted = False

i = 0

while options_padding[i] != 0:
      if options_padding[i] == 1:
            i+=1
            pass
      
      if options_padding[i] == 2:
            i += 1
            option_len = options_padding[i] - 2
            i += 1
            maximum_segment_size = int.from_bytes(options_padding[i:i+option_len], 'big')
            i += option_len
            pass
            
      if options_padding[i] == 3:
            i += 1
            option_len = options_padding[i] - 2
            i += 1
            window_scale = int.from_bytes(options_padding[i:i+option_len], 'big')
            i += option_len
            pass

      if options_padding[i] == 8:
            i += 1
            option_len = options_padding[i] - 2
            i += 1
            timestamp_value = int.from_bytes(options_padding[i:i+(option_len // 2)], 'big')
            i += (option_len // 2)
            timestamp_echo_reply = int.from_bytes(options_padding[i:i+(option_len // 2)], 'big')
            i += (option_len // 2)
            pass
      
      if options_padding[i] == 4:
            sack_permitted = True
            i += 2
            pass
      
print(f'TCP Maximum Segment size is {maximum_segment_size}')
print(f'TCP Window Scale is {window_scale}')
print(f'TCP Timestamp Value is {timestamp_value}')
print(f'TCP Timestamp Echo Reply is {timestamp_echo_reply}')
print(f'TCP SACK Permitted is {sack_permitted}')


f.close()