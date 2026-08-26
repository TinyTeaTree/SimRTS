package main

import (
	"fmt"
	"log"
	"net"
	"time"
)

const (
	udpMagic     = "RTS1"
	udpMaxPacket = 1200
	udpMaxIDLen  = 64
)

const (
	udpHello      byte = 1
	udpAck        byte = 2
	udpOrder      byte = 3
	udpPing       byte = 4
	udpPong       byte = 5
	udpKickoff    byte = 6
	udpKickoffAck byte = 7
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

	go broadcastKickoffs(conn, rooms)

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

func broadcastKickoffs(conn *net.UDPConn, rooms *RoomStore) {
	ticker := time.NewTicker(kickoffRepeat)
	defer ticker.Stop()
	for range ticker.C {
		for _, job := range rooms.KickoffBroadcasts(time.Now()) {
			packet, err := encodeUDPKickoff(job.RoomID, job.KickoffID, job.RemainingMs)
			if err != nil {
				continue
			}
			for _, addr := range job.Addrs {
				_, _ = conn.WriteToUDP(packet, addr)
			}
		}
	}
}

func handleUDP(conn *net.UDPConn, sessions *SessionStore, rooms *RoomStore, packet []byte, from *net.UDPAddr) {
	kind, roomID, playerID, token, off, err := decodeUDPHeader(packet)
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

	case udpPing:
		if !rooms.IsSeated(roomID, playerID) {
			return
		}
		seq, _, err := readU32(packet, off)
		if err != nil {
			return
		}
		pong, err := encodeUDPPong(roomID, playerID, seq)
		if err != nil {
			return
		}
		_, _ = conn.WriteToUDP(pong, from)

	case udpKickoffAck:
		if !rooms.IsSeated(roomID, playerID) {
			return
		}
		id, _, err := readU32(packet, off)
		if err != nil {
			return
		}
		_ = rooms.AckKickoff(roomID, playerID, id)

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

func decodeUDPHeader(packet []byte) (kind byte, roomID, playerID, token string, off int, err error) {
	if len(packet) < 5 || string(packet[:4]) != udpMagic {
		return 0, "", "", "", 0, fmt.Errorf("bad magic")
	}
	kind = packet[4]
	off = 5
	roomID, off, err = readLenString(packet, off)
	if err != nil {
		return 0, "", "", "", 0, err
	}
	playerID, off, err = readLenString(packet, off)
	if err != nil {
		return 0, "", "", "", 0, err
	}
	if kind == udpHello {
		token, off, err = readLenString(packet, off)
		if err != nil {
			return 0, "", "", "", 0, err
		}
	}
	return kind, roomID, playerID, token, off, nil
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

func encodeUDPPong(roomID, playerID string, seq uint32) ([]byte, error) {
	buf := make([]byte, 0, 16+len(roomID)+len(playerID))
	buf = append(buf, udpMagic...)
	buf = append(buf, udpPong)
	var err error
	buf, err = appendLenString(buf, roomID)
	if err != nil {
		return nil, err
	}
	buf, err = appendLenString(buf, playerID)
	if err != nil {
		return nil, err
	}
	buf = appendU32(buf, seq)
	if len(buf) > udpMaxPacket {
		return nil, fmt.Errorf("pong too large")
	}
	return buf, nil
}

func encodeUDPKickoff(roomID string, id, remainingMs uint32) ([]byte, error) {
	buf := make([]byte, 0, 16+len(roomID))
	buf = append(buf, udpMagic...)
	buf = append(buf, udpKickoff)
	var err error
	buf, err = appendLenString(buf, roomID)
	if err != nil {
		return nil, err
	}
	buf, err = appendLenString(buf, "")
	if err != nil {
		return nil, err
	}
	buf = appendU32(buf, id)
	buf = appendU32(buf, remainingMs)
	if len(buf) > udpMaxPacket {
		return nil, fmt.Errorf("kickoff too large")
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

func appendU32(buf []byte, v uint32) []byte {
	return append(buf, byte(v), byte(v>>8), byte(v>>16), byte(v>>24))
}

func readU32(packet []byte, off int) (uint32, int, error) {
	if off+4 > len(packet) {
		return 0, off, fmt.Errorf("short packet")
	}
	v := uint32(packet[off]) |
		uint32(packet[off+1])<<8 |
		uint32(packet[off+2])<<16 |
		uint32(packet[off+3])<<24
	return v, off + 4, nil
}
