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
		_, seat, err := rooms.Seat(roomID, playerID, from)
		if err != nil {
			return
		}
		ack, err := encodeUDPAck(roomID, playerID, seat)
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
		cmd, acks, piggybacks, ackEnd, err := parseIncomingOrder(packet, off)
		if err != nil {
			return
		}
		bounces, err := rooms.PrepareOrderBounces(roomID, playerID, packet[:ackEnd], cmd, acks, piggybacks)
		if err != nil {
			return
		}
		for _, bounce := range bounces {
			_, _ = conn.WriteToUDP(bounce.Packet, bounce.Addr)
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

func encodeUDPAck(roomID, playerID string, seat byte) ([]byte, error) {
	buf := make([]byte, 0, 9+len(roomID)+len(playerID))
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
	buf = append(buf, seat)
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

func readU8(packet []byte, off int) (byte, int, error) {
	if off >= len(packet) {
		return 0, off, fmt.Errorf("short packet")
	}
	return packet[off], off + 1, nil
}

func readU16(packet []byte, off int) (uint16, int, error) {
	if off+2 > len(packet) {
		return 0, off, fmt.Errorf("short packet")
	}
	v := uint16(packet[off]) | uint16(packet[off+1])<<8
	return v, off + 2, nil
}

func readI32(packet []byte, off int) (int32, int, error) {
	u, off, err := readU32(packet, off)
	if err != nil {
		return 0, off, err
	}
	return int32(u), off, nil
}

func appendI32(buf []byte, v int32) []byte {
	return appendU32(buf, uint32(v))
}

func appendU16(buf []byte, v uint16) []byte {
	return append(buf, byte(v), byte(v>>8))
}

func appendU64(buf []byte, v uint64) []byte {
	buf = appendU32(buf, uint32(v))
	return appendU32(buf, uint32(v>>32))
}

func readU64(packet []byte, off int) (uint64, int, error) {
	lo, off, err := readU32(packet, off)
	if err != nil {
		return 0, off, err
	}
	hi, off, err := readU32(packet, off)
	if err != nil {
		return 0, off, err
	}
	return uint64(lo) | uint64(hi)<<32, off, nil
}

type ackEntry struct {
	Seat byte
	Last uint32
}

type orderCommand struct {
	Type       byte
	IsNext     byte
	TargetX    int32
	TargetY    int32
	UnitIDs    []int32
	Seat       byte
	OrderID    uint32
	ActualTick int32
	HashTick   int32
	StateHash  uint64
	Body       []byte
}

func parseCommandBody(packet []byte, off int) (orderCommand, int, error) {
	start := off
	var cmd orderCommand
	var err error
	cmd.Type, off, err = readU8(packet, off)
	if err != nil {
		return orderCommand{}, off, err
	}
	cmd.IsNext, off, err = readU8(packet, off)
	if err != nil {
		return orderCommand{}, off, err
	}
	cmd.TargetX, off, err = readI32(packet, off)
	if err != nil {
		return orderCommand{}, off, err
	}
	cmd.TargetY, off, err = readI32(packet, off)
	if err != nil {
		return orderCommand{}, off, err
	}
	count, off, err := readU16(packet, off)
	if err != nil {
		return orderCommand{}, off, err
	}
	if count > 64 {
		return orderCommand{}, off, fmt.Errorf("too many units")
	}
	cmd.UnitIDs = make([]int32, 0, count)
	for i := uint16(0); i < count; i++ {
		id, next, err := readI32(packet, off)
		if err != nil {
			return orderCommand{}, next, err
		}
		cmd.UnitIDs = append(cmd.UnitIDs, id)
		off = next
	}
	cmd.Seat, off, err = readU8(packet, off)
	if err != nil {
		return orderCommand{}, off, err
	}
	cmd.OrderID, off, err = readU32(packet, off)
	if err != nil {
		return orderCommand{}, off, err
	}
	cmd.ActualTick, off, err = readI32(packet, off)
	if err != nil {
		return orderCommand{}, off, err
	}
	cmd.HashTick, off, err = readI32(packet, off)
	if err != nil {
		return orderCommand{}, off, err
	}
	cmd.StateHash, off, err = readU64(packet, off)
	if err != nil {
		return orderCommand{}, off, err
	}
	cmd.Body = append([]byte(nil), packet[start:off]...)
	return cmd, off, nil
}

func parseAckVector(packet []byte, off int) ([]ackEntry, int, error) {
	n, off, err := readU8(packet, off)
	if err != nil {
		return nil, off, err
	}
	out := make([]ackEntry, 0, n)
	for i := 0; i < int(n); i++ {
		seat, next, err := readU8(packet, off)
		if err != nil {
			return nil, next, err
		}
		last, next, err := readU32(packet, next)
		if err != nil {
			return nil, next, err
		}
		out = append(out, ackEntry{Seat: seat, Last: last})
		off = next
	}
	return out, off, nil
}

func parseIncomingOrder(packet []byte, off int) (orderCommand, []ackEntry, [][]byte, int, error) {
	cmd, off, err := parseCommandBody(packet, off)
	if err != nil {
		return orderCommand{}, nil, nil, off, err
	}
	acks, off, err := parseAckVector(packet, off)
	if err != nil {
		return orderCommand{}, nil, nil, off, err
	}
	ackEnd := off
	_, off, err = parseAckVector(packet, off)
	if err != nil {
		return orderCommand{}, nil, nil, off, err
	}
	n, off, err := readU8(packet, off)
	if err != nil {
		return orderCommand{}, nil, nil, off, err
	}
	piggybacks := make([][]byte, 0, n)
	for i := 0; i < int(n); i++ {
		pb, next, err := parseCommandBody(packet, off)
		if err != nil {
			return orderCommand{}, nil, nil, next, err
		}
		piggybacks = append(piggybacks, pb.Body)
		off = next
	}
	return cmd, acks, piggybacks, ackEnd, nil
}

func encodeCommandBody(buf []byte, seat byte, orderID uint32, actualTick int32, unitIDs []int32, targetX, targetY int32, isNext bool, hashTick int32, stateHash uint64) []byte {
	if isNext {
		buf = append(buf, 0, 1)
	} else {
		buf = append(buf, 0, 0)
	}
	buf = appendI32(buf, targetX)
	buf = appendI32(buf, targetY)
	buf = appendU16(buf, uint16(len(unitIDs)))
	for _, id := range unitIDs {
		buf = appendI32(buf, id)
	}
	buf = append(buf, seat)
	buf = appendU32(buf, orderID)
	buf = appendI32(buf, actualTick)
	buf = appendI32(buf, hashTick)
	return appendU64(buf, stateHash)
}

func encodeAckVector(buf []byte, acks []ackEntry) []byte {
	buf = append(buf, byte(len(acks)))
	for _, ack := range acks {
		buf = append(buf, ack.Seat)
		buf = appendU32(buf, ack.Last)
	}
	return buf
}

func encodeTestOrder(roomID, playerID string, seat byte, orderID uint32, actualTick int32, unitIDs []int32, acks []ackEntry, piggybacks [][]byte) ([]byte, error) {
	buf := append([]byte{}, udpMagic...)
	buf = append(buf, udpOrder)
	var err error
	buf, err = appendLenString(buf, roomID)
	if err != nil {
		return nil, err
	}
	buf, err = appendLenString(buf, playerID)
	if err != nil {
		return nil, err
	}
	buf = encodeCommandBody(buf, seat, orderID, actualTick, unitIDs, 0, 0, false, 0, 0)
	buf = encodeAckVector(buf, acks)
	buf = encodeAckVector(buf, nil)
	if len(piggybacks) > 255 {
		return nil, fmt.Errorf("too many piggybacks")
	}
	buf = append(buf, byte(len(piggybacks)))
	for _, body := range piggybacks {
		buf = append(buf, body...)
	}
	if len(buf) > udpMaxPacket {
		return nil, fmt.Errorf("order too large")
	}
	return buf, nil
}
