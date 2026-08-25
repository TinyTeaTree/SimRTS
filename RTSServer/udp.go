package main

import (
	"fmt"
	"log"
	"net"
)

const (
	udpMagic     = "RTS1"
	udpMaxPacket = 1200
	udpMaxIDLen  = 64
)

const (
	udpHello byte = 1
	udpAck   byte = 2
	udpOrder byte = 3
)

func serveUDP(bind string, port int, sessions *SessionStore, rooms *RoomStore) {
	addr := net.JoinHostPort(bind, fmt.Sprintf("%d", port))
	udpAddr, err := net.ResolveUDPAddr("udp", addr)
	if err != nil {
		log.Fatalf("udp resolve: %v", err)
	}
	conn, err := net.ListenUDP("udp", udpAddr)
	if err != nil {
		log.Fatalf("udp listen: %v", err)
	}
	log.Printf("RTSServer UDP relay on udp://%s", addr)

	buf := make([]byte, udpMaxPacket)
	for {
		n, from, err := conn.ReadFromUDP(buf)
		if err != nil {
			log.Printf("udp read: %v", err)
			continue
		}
		if n > udpMaxPacket {
			continue
		}
		handleUDP(conn, sessions, rooms, buf[:n], from)
	}
}

func handleUDP(conn *net.UDPConn, sessions *SessionStore, rooms *RoomStore, packet []byte, from *net.UDPAddr) {
	kind, roomID, playerID, token, err := decodeUDPHeader(packet)
	if err != nil {
		return
	}

	switch kind {
	case udpHello:
		session, ok := sessions.Lookup(token)
		if !ok || session.PlayerID != playerID {
			return
		}
		if _, err := rooms.Seat(roomID, playerID, from); err != nil {
			return
		}
		ack, err := encodeUDPAck(roomID, playerID)
		if err != nil {
			return
		}
		_, _ = conn.WriteToUDP(ack, from)

	case udpOrder:
		addrs, err := rooms.RelayAddrs(roomID, playerID)
		if err != nil {
			return
		}
		for _, addr := range addrs {
			_, _ = conn.WriteToUDP(packet, addr)
		}
	}
}

func decodeUDPHeader(packet []byte) (kind byte, roomID, playerID, token string, err error) {
	if len(packet) < 5 || string(packet[:4]) != udpMagic {
		return 0, "", "", "", fmt.Errorf("bad magic")
	}
	kind = packet[4]
	off := 5
	roomID, off, err = readLenString(packet, off)
	if err != nil {
		return 0, "", "", "", err
	}
	playerID, off, err = readLenString(packet, off)
	if err != nil {
		return 0, "", "", "", err
	}
	if kind == udpHello {
		token, _, err = readLenString(packet, off)
		if err != nil {
			return 0, "", "", "", err
		}
	}
	return kind, roomID, playerID, token, nil
}

func encodeUDPAck(roomID, playerID string) ([]byte, error) {
	buf := make([]byte, 0, 8+len(roomID)+len(playerID))
	buf = append(buf, udpMagic...)
	buf = append(buf, udpAck)
	var err error
	buf, err = appendLenString(buf, roomID)
	if err != nil {
		return nil, err
	}
	buf, err = appendLenString(buf, playerID)
	if err != nil {
		return nil, err
	}
	if len(buf) > udpMaxPacket {
		return nil, fmt.Errorf("ack too large")
	}
	return buf, nil
}

func readLenString(packet []byte, off int) (string, int, error) {
	if off >= len(packet) {
		return "", off, fmt.Errorf("short packet")
	}
	n := int(packet[off])
	off++
	if n > udpMaxIDLen || off+n > len(packet) {
		return "", off, fmt.Errorf("bad string")
	}
	return string(packet[off : off+n]), off + n, nil
}

func appendLenString(buf []byte, s string) ([]byte, error) {
	if len(s) > udpMaxIDLen {
		return nil, fmt.Errorf("string too long")
	}
	buf = append(buf, byte(len(s)))
	buf = append(buf, s...)
	return buf, nil
}
