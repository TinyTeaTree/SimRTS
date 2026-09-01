package main

import (
	"net"
	"testing"
	"time"
)

func TestUDPHelloSeatsAndRelaysOrder(t *testing.T) {
	sessions := NewSessionStore()
	rooms := NewRoomStore()
	if _, err := rooms.Create("alpha"); err != nil {
		t.Fatal(err)
	}
	session, err := sessions.Login("alice")
	if err != nil {
		t.Fatal(err)
	}

	server, err := net.ListenUDP("udp", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 0})
	if err != nil {
		t.Fatal(err)
	}
	defer server.Close()
	serverAddr := server.LocalAddr().(*net.UDPAddr)
	go func() {
		buf := make([]byte, 1200)
		for {
			n, from, err := server.ReadFromUDP(buf)
			if err != nil {
				return
			}
			handleUDP(server, sessions, rooms, buf[:n], from)
		}
	}()

	client, err := net.ListenUDP("udp", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 0})
	if err != nil {
		t.Fatal(err)
	}
	defer client.Close()
	_ = client.SetReadDeadline(time.Now().Add(2 * time.Second))

	hello, err := encodeTestHello("alpha", session.PlayerID, session.Token)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := client.WriteToUDP(hello, serverAddr); err != nil {
		t.Fatal(err)
	}

	buf := make([]byte, 1200)
	n, _, err := client.ReadFromUDP(buf)
	if err != nil {
		t.Fatalf("ack: %v", err)
	}
	kind, roomID, playerID, _, off, err := decodeUDPHeader(buf[:n])
	if err != nil || kind != udpAck || roomID != "alpha" || playerID != session.PlayerID {
		t.Fatalf("ack packet kind=%d room=%s player=%s err=%v", kind, roomID, playerID, err)
	}
	seat, _, err := readU8(buf[:n], off)
	if err != nil || seat != 0 {
		t.Fatalf("ack seat=%d err=%v", seat, err)
	}

	listed, err := rooms.Get("alpha")
	if err != nil {
		t.Fatal(err)
	}
	if len(listed.PlayerIDs) != 1 || listed.PlayerIDs[0] != session.PlayerID {
		t.Fatalf("seated %#v", listed.PlayerIDs)
	}
	if len(listed.Seats) != 1 || listed.Seats[0] != 0 {
		t.Fatalf("seats %#v", listed.Seats)
	}

	order, err := encodeTestOrder("alpha", session.PlayerID, 0, 0, 0, nil, []ackEntry{{Seat: 0, Last: 0}}, nil)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := client.WriteToUDP(order, serverAddr); err != nil {
		t.Fatal(err)
	}
	n, _, err = client.ReadFromUDP(buf)
	if err != nil {
		t.Fatalf("relay: %v", err)
	}
	kind, _, _, _, bodyOff, err := decodeUDPHeader(buf[:n])
	if err != nil || kind != udpOrder {
		t.Fatalf("relay kind=%d err=%v", kind, err)
	}
	_, _, _, _, err = parseIncomingOrder(buf[:n], bodyOff)
	if err != nil {
		t.Fatalf("relay body: %v", err)
	}
}

func encodeTestHello(roomID, playerID, token string) ([]byte, error) {
	buf := append([]byte{}, udpMagic...)
	buf = append(buf, udpHello)
	var err error
	buf, err = appendLenString(buf, roomID)
	if err != nil {
		return nil, err
	}
	buf, err = appendLenString(buf, playerID)
	if err != nil {
		return nil, err
	}
	return appendLenString(buf, token)
}

func TestUDPPingEchoesToSender(t *testing.T) {
	sessions := NewSessionStore()
	rooms := NewRoomStore()
	if _, err := rooms.Create("alpha"); err != nil {
		t.Fatal(err)
	}
	session, err := sessions.Login("alice")
	if err != nil {
		t.Fatal(err)
	}

	server, err := net.ListenUDP("udp", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 0})
	if err != nil {
		t.Fatal(err)
	}
	defer server.Close()
	serverAddr := server.LocalAddr().(*net.UDPAddr)
	go func() {
		buf := make([]byte, 1200)
		for {
			n, from, err := server.ReadFromUDP(buf)
			if err != nil {
				return
			}
			handleUDP(server, sessions, rooms, buf[:n], from)
		}
	}()

	client, err := net.ListenUDP("udp", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 0})
	if err != nil {
		t.Fatal(err)
	}
	defer client.Close()
	_ = client.SetReadDeadline(time.Now().Add(2 * time.Second))

	hello, err := encodeTestHello("alpha", session.PlayerID, session.Token)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := client.WriteToUDP(hello, serverAddr); err != nil {
		t.Fatal(err)
	}
	buf := make([]byte, 1200)
	if _, _, err := client.ReadFromUDP(buf); err != nil {
		t.Fatalf("ack: %v", err)
	}

	ping := append([]byte{}, udpMagic...)
	ping = append(ping, udpPing)
	ping, _ = appendLenString(ping, "alpha")
	ping, _ = appendLenString(ping, session.PlayerID)
	ping = appendU32(ping, 42)
	if _, err := client.WriteToUDP(ping, serverAddr); err != nil {
		t.Fatal(err)
	}
	n, _, err := client.ReadFromUDP(buf)
	if err != nil {
		t.Fatalf("pong: %v", err)
	}
	kind, _, _, _, off, err := decodeUDPHeader(buf[:n])
	if err != nil || kind != udpPong {
		t.Fatalf("pong kind=%d err=%v", kind, err)
	}
	seq, _, err := readU32(buf[:n], off)
	if err != nil || seq != 42 {
		t.Fatalf("pong seq=%d err=%v", seq, err)
	}
}

func helloAndReadAck(t *testing.T, client *net.UDPConn, serverAddr *net.UDPAddr, roomID, playerID, token string) byte {
	t.Helper()
	hello, err := encodeTestHello(roomID, playerID, token)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := client.WriteToUDP(hello, serverAddr); err != nil {
		t.Fatal(err)
	}
	buf := make([]byte, 1200)
	n, _, err := client.ReadFromUDP(buf)
	if err != nil {
		t.Fatalf("ack: %v", err)
	}
	kind, _, _, _, off, err := decodeUDPHeader(buf[:n])
	if err != nil || kind != udpAck {
		t.Fatalf("ack kind=%d err=%v", kind, err)
	}
	seat, _, err := readU8(buf[:n], off)
	if err != nil {
		t.Fatal(err)
	}
	return seat
}

func readOrderBounce(t *testing.T, client *net.UDPConn) (orderCommand, []ackEntry, []ackEntry, [][]byte) {
	t.Helper()
	buf := make([]byte, 1200)
	n, _, err := client.ReadFromUDP(buf)
	if err != nil {
		t.Fatalf("bounce: %v", err)
	}
	kind, _, _, _, off, err := decodeUDPHeader(buf[:n])
	if err != nil || kind != udpOrder {
		t.Fatalf("bounce kind=%d err=%v", kind, err)
	}
	cmd, acks, piggybacks, ackEnd, err := parseIncomingOrder(buf[:n], off)
	if err != nil {
		t.Fatalf("bounce body: %v", err)
	}
	watermarks, _, err := parseAckVector(buf[:n], ackEnd)
	if err != nil {
		t.Fatalf("watermarks: %v", err)
	}
	return cmd, acks, watermarks, piggybacks
}

func TestUDPHelloJoinOrderSeats(t *testing.T) {
	sessions := NewSessionStore()
	rooms := NewRoomStore()
	if _, err := rooms.Create("alpha"); err != nil {
		t.Fatal(err)
	}
	first, err := sessions.Login("alice")
	if err != nil {
		t.Fatal(err)
	}
	second, err := sessions.Login("bob")
	if err != nil {
		t.Fatal(err)
	}

	server, err := net.ListenUDP("udp", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 0})
	if err != nil {
		t.Fatal(err)
	}
	defer server.Close()
	serverAddr := server.LocalAddr().(*net.UDPAddr)
	go func() {
		buf := make([]byte, 1200)
		for {
			n, from, err := server.ReadFromUDP(buf)
			if err != nil {
				return
			}
			handleUDP(server, sessions, rooms, buf[:n], from)
		}
	}()

	alice, err := net.ListenUDP("udp", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 0})
	if err != nil {
		t.Fatal(err)
	}
	defer alice.Close()
	bob, err := net.ListenUDP("udp", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 0})
	if err != nil {
		t.Fatal(err)
	}
	defer bob.Close()
	_ = alice.SetReadDeadline(time.Now().Add(2 * time.Second))
	_ = bob.SetReadDeadline(time.Now().Add(2 * time.Second))

	if seat := helloAndReadAck(t, bob, serverAddr, "alpha", second.PlayerID, second.Token); seat != 0 {
		t.Fatalf("bob should be seat 0, got %d", seat)
	}
	if seat := helloAndReadAck(t, alice, serverAddr, "alpha", first.PlayerID, first.Token); seat != 1 {
		t.Fatalf("alice should be seat 1, got %d", seat)
	}
	listed, err := rooms.Get("alpha")
	if err != nil {
		t.Fatal(err)
	}
	if listed.PlayerIDs[0] != second.PlayerID || listed.PlayerIDs[1] != first.PlayerID {
		t.Fatalf("join order %#v", listed.PlayerIDs)
	}
	if listed.Seats[0] != 0 || listed.Seats[1] != 1 {
		t.Fatalf("seats %#v", listed.Seats)
	}
}

func TestUDPOrderFullyAckedAndClickCache(t *testing.T) {
	sessions := NewSessionStore()
	rooms := NewRoomStore()
	if _, err := rooms.Create("alpha"); err != nil {
		t.Fatal(err)
	}
	aliceSess, err := sessions.Login("alice")
	if err != nil {
		t.Fatal(err)
	}
	bobSess, err := sessions.Login("bob")
	if err != nil {
		t.Fatal(err)
	}

	server, err := net.ListenUDP("udp", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 0})
	if err != nil {
		t.Fatal(err)
	}
	defer server.Close()
	serverAddr := server.LocalAddr().(*net.UDPAddr)
	go func() {
		buf := make([]byte, 1200)
		for {
			n, from, err := server.ReadFromUDP(buf)
			if err != nil {
				return
			}
			handleUDP(server, sessions, rooms, buf[:n], from)
		}
	}()

	alice, err := net.ListenUDP("udp", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 0})
	if err != nil {
		t.Fatal(err)
	}
	defer alice.Close()
	bob, err := net.ListenUDP("udp", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 0})
	if err != nil {
		t.Fatal(err)
	}
	defer bob.Close()
	_ = alice.SetReadDeadline(time.Now().Add(2 * time.Second))
	_ = bob.SetReadDeadline(time.Now().Add(2 * time.Second))

	if helloAndReadAck(t, alice, serverAddr, "alpha", aliceSess.PlayerID, aliceSess.Token) != 0 {
		t.Fatal("alice seat")
	}
	if helloAndReadAck(t, bob, serverAddr, "alpha", bobSess.PlayerID, bobSess.Token) != 1 {
		t.Fatal("bob seat")
	}

	acks := []ackEntry{{Seat: 0, Last: 1}, {Seat: 1, Last: 0}}
	click, err := encodeTestOrder("alpha", aliceSess.PlayerID, 0, 1, 10, []int32{7}, acks, nil)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := alice.WriteToUDP(click, serverAddr); err != nil {
		t.Fatal(err)
	}

	aliceCmd, _, aliceWM, alicePB := readOrderBounce(t, alice)
	bobCmd, _, bobWM, bobPB := readOrderBounce(t, bob)
	if aliceCmd.OrderID != 1 || len(aliceCmd.UnitIDs) != 1 || aliceCmd.UnitIDs[0] != 7 {
		t.Fatalf("alice main %#v", aliceCmd)
	}
	if bobCmd.OrderID != 1 || len(bobCmd.UnitIDs) != 1 {
		t.Fatalf("bob main %#v", bobCmd)
	}
	if watermarkFor(aliceWM, 0) != 0 {
		t.Fatalf("alice fully-acked of self should be bob's 0, got %d", watermarkFor(aliceWM, 0))
	}
	if watermarkFor(bobWM, 0) != 1 {
		t.Fatalf("bob fully-acked of alice should be alice's 1, got %d", watermarkFor(bobWM, 0))
	}
	if len(alicePB) != 0 {
		t.Fatalf("alice should not receive her own click piggyback, got %d", len(alicePB))
	}
	if len(bobPB) != 0 {
		t.Fatalf("first bounce already carries the click, no extra piggyback, got %d", len(bobPB))
	}

	emptyAcks := []ackEntry{{Seat: 0, Last: 1}, {Seat: 1, Last: 0}}
	empty, err := encodeTestOrder("alpha", aliceSess.PlayerID, 0, 1, 11, nil, emptyAcks, nil)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := alice.WriteToUDP(empty, serverAddr); err != nil {
		t.Fatal(err)
	}
	_, _, _, _ = readOrderBounce(t, alice)
	emptyCmd, _, _, emptyPB := readOrderBounce(t, bob)
	if len(emptyCmd.UnitIDs) != 0 {
		t.Fatalf("empty main should have no units")
	}
	if len(emptyPB) != 1 {
		t.Fatalf("bob missing click should be piggybacked, got %d", len(emptyPB))
	}
	pb, _, err := parseCommandBody(emptyPB[0], 0)
	if err != nil || pb.OrderID != 1 || len(pb.UnitIDs) != 1 || pb.UnitIDs[0] != 7 {
		t.Fatalf("piggyback %#v err=%v", pb, err)
	}
}

func TestUDPOrderDoesNotCacheEmpty(t *testing.T) {
	sessions := NewSessionStore()
	rooms := NewRoomStore()
	if _, err := rooms.Create("alpha"); err != nil {
		t.Fatal(err)
	}
	aliceSess, err := sessions.Login("alice")
	if err != nil {
		t.Fatal(err)
	}
	bobSess, err := sessions.Login("bob")
	if err != nil {
		t.Fatal(err)
	}

	server, err := net.ListenUDP("udp", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 0})
	if err != nil {
		t.Fatal(err)
	}
	defer server.Close()
	serverAddr := server.LocalAddr().(*net.UDPAddr)
	go func() {
		buf := make([]byte, 1200)
		for {
			n, from, err := server.ReadFromUDP(buf)
			if err != nil {
				return
			}
			handleUDP(server, sessions, rooms, buf[:n], from)
		}
	}()

	alice, err := net.ListenUDP("udp", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 0})
	if err != nil {
		t.Fatal(err)
	}
	defer alice.Close()
	bob, err := net.ListenUDP("udp", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 0})
	if err != nil {
		t.Fatal(err)
	}
	defer bob.Close()
	_ = alice.SetReadDeadline(time.Now().Add(2 * time.Second))
	_ = bob.SetReadDeadline(time.Now().Add(2 * time.Second))

	helloAndReadAck(t, alice, serverAddr, "alpha", aliceSess.PlayerID, aliceSess.Token)
	helloAndReadAck(t, bob, serverAddr, "alpha", bobSess.PlayerID, bobSess.Token)

	acks := []ackEntry{{Seat: 0, Last: 0}, {Seat: 1, Last: 0}}
	empty, err := encodeTestOrder("alpha", aliceSess.PlayerID, 0, 0, 1, nil, acks, nil)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := alice.WriteToUDP(empty, serverAddr); err != nil {
		t.Fatal(err)
	}
	_, _, _, _ = readOrderBounce(t, alice)
	_, _, _, _ = readOrderBounce(t, bob)

	later, err := encodeTestOrder("alpha", aliceSess.PlayerID, 0, 0, 2, nil, acks, nil)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := alice.WriteToUDP(later, serverAddr); err != nil {
		t.Fatal(err)
	}
	_, _, _, _ = readOrderBounce(t, alice)
	_, _, _, pbs := readOrderBounce(t, bob)
	if len(pbs) != 0 {
		t.Fatalf("empties must not be cached/piggybacked, got %d", len(pbs))
	}
}

func watermarkFor(entries []ackEntry, seat byte) uint32 {
	for _, entry := range entries {
		if entry.Seat == seat {
			return entry.Last
		}
	}
	return 0
}
