package main

import "testing"

func TestRoomLifecycle(t *testing.T) {
	store := NewRoomStore()

	if _, err := store.Create("alpha"); err != nil {
		t.Fatalf("create: %v", err)
	}
	if _, err := store.Create("alpha"); err == nil {
		t.Fatal("expected duplicate create to fail")
	}

	room, err := store.Join("alpha", "p2")
	if err != nil {
		t.Fatalf("join p2: %v", err)
	}
	room, err = store.Join("alpha", "p1")
	if err != nil {
		t.Fatalf("join p1: %v", err)
	}
	if len(room.PlayerIDs) != 2 || room.PlayerIDs[0] != "p1" || room.PlayerIDs[1] != "p2" {
		t.Fatalf("players = %#v", room.PlayerIDs)
	}

	again, err := store.Join("alpha", "p1")
	if err != nil {
		t.Fatalf("idempotent join: %v", err)
	}
	if len(again.PlayerIDs) != 2 {
		t.Fatalf("duplicate join changed roster: %#v", again.PlayerIDs)
	}

	left, err := store.Leave("alpha", "p1")
	if err != nil {
		t.Fatalf("leave: %v", err)
	}
	if len(left.PlayerIDs) != 1 || left.PlayerIDs[0] != "p2" {
		t.Fatalf("after leave: %#v", left.PlayerIDs)
	}

	rooms := store.List()
	if len(rooms) != 1 || rooms[0].ID != "alpha" {
		t.Fatalf("list = %#v", rooms)
	}
}
