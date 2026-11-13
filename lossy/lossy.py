import sys
from datetime import datetime as dt
from socket import inet_ntop, AF_INET


def parse_packet(packet: bytes):
    dmac_destination_mac_address = packet[0:6]
    smac_destination_mac_address = packet[6:12]
    type_protocol_type = packet[12:14]

    ip_packet_raw = packet[14:]

    # start of ip header processing -----

    version_ihl = ip_packet_raw[0]
    version = version_ihl >> 4
    ihl = version_ihl & 0x0F

    type_of_service = ip_packet_raw[1]
    total_length = int.from_bytes(ip_packet_raw[2:4], "big")
    identification = ip_packet_raw[4:6]
    flags_fragment_offset_raw = ip_packet_raw[6:8]
    flags = flags_fragment_offset_raw[0] >> 5
    fragment_offset = int.from_bytes(flags_fragment_offset_raw, "big") & 0x1FFF
    ttl = ip_packet_raw[8]
    protocol = ip_packet_raw[9]
    header_checksum = ip_packet_raw[10:12]
    source_address = ip_packet_raw[12:16]
    destination_address = ip_packet_raw[16:20]

    response = {
        "packet_hex": packet.hex("\\"),
        "ip_ihl": ihl,
        "ip_version": version,
        "ip_precedence": type_of_service >> 5,
        "ip_delay": "Normal" if (type_of_service >> 4) & 0b1 == 0 else "Low",
        "ip_throughput": "Normal" if (type_of_service >> 3) & 0b1 == 0 else "High",
        "ip_reliability": "Normal" if (type_of_service >> 2) & 0b1 == 0 else "High",
        "ip_total_length": total_length,
        "ip_identification": identification,
        "ip_flags": {"reserved": flags & 0b100, "df": flags & 0b010, "mf": flags & 0b1},
        "ip_fragment_offset": fragment_offset,
        "ip_ttl": ttl,
        "ip_protocol": protocol,
        "ip_header_checksum": int.from_bytes(header_checksum, "big"),
        "ip_source_address": inet_ntop(AF_INET, source_address),
        "ip_destination_address": inet_ntop(AF_INET, destination_address),
    }

    # tcp packet raw must be of length 44
    tcp_packet_raw = ip_packet_raw[20:]
    source_port = tcp_packet_raw[0:2]
    destination_port = tcp_packet_raw[2:4]
    sequence_number = tcp_packet_raw[4:8]
    acknowledgment_number = tcp_packet_raw[8:12]
    data_offset_reserved = tcp_packet_raw[12]
    data_offset = data_offset_reserved >> 4
    reserved = data_offset_reserved & 0x0F
    flags = tcp_packet_raw[13]
    window = tcp_packet_raw[14:16]
    checksum = tcp_packet_raw[16:18]
    urgent_pointer = tcp_packet_raw[18:20]
    options_padding = tcp_packet_raw[20 : data_offset * 4]

    data = tcp_packet_raw[data_offset * 4 : total_length - ihl * 4]

    options_size = (data_offset - 5) * 32

    if options_size == 0:
        pass

    maximum_segment_size = 0
    window_scale = 0
    timestamp_value = 0
    timestamp_echo_reply = 0
    sack_permitted = False

    i = 0
    while i < len(options_padding) and options_padding[i] != 0:
        if options_padding[i] == 1:
            i += 1
            continue

        if options_padding[i] == 2:
            i += 1
            option_len = options_padding[i] - 2
            i += 1
            maximum_segment_size = int.from_bytes(
                options_padding[i : i + option_len], "big"
            )
            i += option_len
            continue

        if options_padding[i] == 3:
            i += 1
            option_len = options_padding[i] - 2
            i += 1
            window_scale = int.from_bytes(options_padding[i : i + option_len], "big")
            i += option_len
            continue

        if options_padding[i] == 8:
            i += 1
            option_len = options_padding[i] - 2
            i += 1
            timestamp_value = int.from_bytes(
                options_padding[i : i + (option_len // 2)], "big"
            )
            i += option_len // 2
            timestamp_echo_reply = int.from_bytes(
                options_padding[i : i + (option_len // 2)], "big"
            )
            i += option_len // 2
            continue

        if options_padding[i] == 4:
            sack_permitted = True
            i += 2
            continue

    response = {
        "tcp_source_port": int.from_bytes(source_port, "big"),
        "tcp_destination_port": int.from_bytes(destination_port, "big"),
        "tcp_sequence_number": int.from_bytes(sequence_number, "big"),
        "tcp_acknowledgment_number": int.from_bytes(acknowledgment_number, "big"),
        "tcp_data_offset": data_offset,
        "tcp_reserved": reserved,
        "tcp_flags": {
            "cwr": bool(flags & 0b10000000),
            "ecn": bool(flags & 0b01000000),
            "urgent": bool(flags & 0b00100000),
            "ack": bool(flags & 0b00010000),
            "psh": bool(flags & 0b1000),
            "rst": bool(flags & 0b0100),
            "syn": bool(flags & 0b0010),
            "fin": bool(flags & 0b0001),
        },
        "tcp_window": int.from_bytes(window, "big"),
        "tcp_checksum": int.from_bytes(checksum, "big"),
        "tcp_urgent_pointer": int.from_bytes(urgent_pointer, "big"),
        "tcp_options_padding": options_padding,
        "tcp_maximum_segment_size": maximum_segment_size,
        "tcp_window_scale": window_scale,
        "tcp_timestamp_value": timestamp_value,
        "tcp_timestamp_echo_reply": timestamp_echo_reply,
        "tcp_sack_permitted": sack_permitted,
        **response,
    }

    return response, data


def log_packet(packet: dict):
    # print(packet["packet_hex"])
    print(f"ip protocol version is {packet['ip_version']}")
    print(f"IHL (internet header length) is {packet['ip_ihl']}")
    print(
        f"Type of Service: precedence = {packet['ip_precedence']}, "
        f"Delay = {packet['ip_delay']} "
        f"Throughput = {packet['ip_throughput']} "
        f"Reliability = {packet['ip_reliability']}"
    )
    print(f"total length is {packet['ip_total_length']}")
    print(f"identification is {packet['ip_identification']}")
    print(
        f"flags: reserved (must be zero) = {packet['ip_flags']['reserved']}; "
        f"DF = {"Don't fragment" if packet['ip_flags']['df'] > 0 else "May Fragment"}; MF = {"More fragments" if packet['ip_flags']['mf'] > 0 else "Last Fragment"}"
    )
    print(f"fragment offset is {packet['ip_fragment_offset']}")
    print(f"ttl is {packet['ip_ttl']}")
    print(f"protocol is {packet['ip_protocol']}")
    print(f"header checksum is {packet['ip_header_checksum']}")
    print(f"source address is {packet['ip_source_address']}")
    print(f"destination address is {packet['ip_destination_address']}")
    print("\n")

    print(f"TCP Source Port is {packet['tcp_source_port']}")
    print(f"TCP Destination Port is {packet['tcp_destination_port']}")
    print(f"TCP Sequence Number is {packet['tcp_sequence_number']}")
    print(f"TCP Acknowledgment Number is {packet['tcp_acknowledgment_number']}")
    print(f"TCP Data Offset is {packet['tcp_data_offset']}")
    print(f"TCP Reserved is {packet['tcp_reserved']}")
    print(
        f"TCP Flags are: Urgent - {packet['tcp_flags']['urgent']}, "
        f"CWE (Congestion Window Reduced) - {packet['tcp_flags']['cwr']}, "
        f"ECN (Explicit Congestion Notifcation) - {packet['tcp_flags']['ecn']}, "
        f"ACK - {packet['tcp_flags']['ack']}, "
        f"PSH - {packet['tcp_flags']['psh']}, "
        f"RST - {packet['tcp_flags']['rst']}, "
        f"SYN - {packet['tcp_flags']['syn']}, "
        f"FIN - {packet['tcp_flags']['fin']}"
    )
    print(f"TCP Window is {packet['tcp_window']}")
    print(f"TCP Checksum is {packet['tcp_checksum']}")
    print(f"TCP Urgent Pointer is {packet['tcp_urgent_pointer']}")
    print("Options and padding are:")
    print(packet["tcp_options_padding"].hex("\\"))
    print(f"TCP Maximum Segment size is {packet['tcp_maximum_segment_size']}")
    print(f"TCP Window Scale is {packet['tcp_window_scale']}")
    print(f"TCP Timestamp Value is {packet['tcp_timestamp_value']}")
    print(f"TCP Timestamp Echo Reply is {packet['tcp_timestamp_echo_reply']}")
    print(f"TCP SACK Permitted is {packet['tcp_sack_permitted']}")


f = open("lossy.pcap", "rb")

magic_number = f.read(4)
major_version = f.read(2)
minor_version = f.read(2)
timezone_offset = f.read(4)
timestamp_accuracy = f.read(4)
snapshot_length = int.from_bytes(f.read(4), sys.byteorder)
link_layer_header_type = f.read(4)

print(f"number is {int.from_bytes(magic_number, sys.byteorder):x}")
print(f"major version is {int.from_bytes(major_version, sys.byteorder):x}")
print(f"minor version is {int.from_bytes(minor_version, sys.byteorder):x}")
print(f"timezone offset is {int.from_bytes(timezone_offset, sys.byteorder)}")
print(f"timestamp accuracy is {int.from_bytes(timestamp_accuracy, sys.byteorder)}")
print(f"snapshot length is {snapshot_length}")
print(
    f"link layer header type is {int.from_bytes(link_layer_header_type, sys.byteorder)}\n"
)


i = 0
while i < snapshot_length:
    timestamp_seconds = f.read(4)
    timestamp_microseconds = f.read(4)
    captured_packet_data_length = int.from_bytes(f.read(4), sys.byteorder)
    untruncated_packet_data_length = f.read(4)

    print(f"timestamps in seconds {int.from_bytes(timestamp_seconds, sys.byteorder)}")
    print(
        f"timestamps in microseconds {int.from_bytes(timestamp_microseconds, sys.byteorder)}"
    )
    print(f"captured packet data length {captured_packet_data_length}")
    print(
        f"untruncated captured packet data length {int.from_bytes(untruncated_packet_data_length, sys.byteorder)}\n"
    )

    packet_raw = f.read(captured_packet_data_length)

    response, data = parse_packet(packet_raw)

    print("=== Packet Start ===")
    log_packet(response)
    print(f"Data len sent {len(data)}")
    if len(data) > 0:
        print(f"Data is:")
        print(data)

    print("=== Packet End ===")

    print("")

    i += captured_packet_data_length

# print(f'timestamp as date {dt.fromtimestamp(int.from_bytes(timestamp_seconds, sys.byteorder))}')


f.close()
