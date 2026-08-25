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
	kind, roomID, playerID, _, err := decodeUDPHeader(buf[:n])
	if err != nil || kind != udpAck || roomID != "alpha" || playerID != session.PlayerID {
		t.Fatalf("ack packet kind=%d room=%s player=%s err=%v", kind, roomID, playerID, err)
	}

	listed, err := rooms.Get("alpha")
	if err != nil {
		t.Fatal(err)
	}
	if len(listed.PlayerIDs) != 1 || listed.PlayerIDs[0] != session.PlayerID {
		t.Fatalf("seated %#v", listed.PlayerIDs)
	}

	order := append([]byte{}, udpMagic...)
	order = append(order, udpOrder)
	order, _ = appendLenString(order, "alpha")
	order, _ = appendLenString(order, session.PlayerID)
	if _, err := client.WriteToUDP(order, serverAddr); err != nil {
		t.Fatal(err)
	}
	n, _, err = client.ReadFromUDP(buf)
	if err != nil {
		t.Fatalf("relay: %v", err)
	}
	kind, _, _, _, err = decodeUDPHeader(buf[:n])
	if err != nil || kind != udpOrder {
		t.Fatalf("relay kind=%d err=%v", kind, err)
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
